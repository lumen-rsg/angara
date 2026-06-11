#include "MarkSweepGC.h"
#include "RuntimeBuilder.h"

using namespace llvm;

namespace angara {

MarkSweepGC::MarkSweepGC(LLVMContext& ctx, Module& module, IRBuilder<>& builder)
    : m_ctx(ctx), m_module(module), m_builder(builder) {}

// ---------------------------------------------------------------------------
// Type generation
// ---------------------------------------------------------------------------

void MarkSweepGC::generateTypes() {
    auto& ctx = m_ctx;

    // ObjHeader: { i32 type, i32 meta, i8* next }
    // meta packs: byte 0 = color (WHITE/GRAY/BLACK), byte 1 = is_unique flag
    m_obj_header_type = StructType::create(ctx, {
        Type::getInt32Ty(ctx),     // field 0: type   (OBJ_STRING, OBJ_LIST, ...)
        Type::getInt32Ty(ctx),     // field 1: meta   (color | (is_unique << 8))
        PointerType::get(ctx, 0)   // field 2: next   (intrusive allocation list)
    }, "ObjHeader");

    // GcThreadState: per-thread data for the collector
    // { i8* self, i8* next_thread, i8* root_frames, i1 waiting }
    m_gc_thread_state_type = StructType::create(ctx, {
        PointerType::get(ctx, 0),  // self (pointer to own allocation)
        PointerType::get(ctx, 0),  // next_thread (linked list of threads)
        PointerType::get(ctx, 0),  // root_frames (stack of GcRootFrame pointers)
        Type::getInt1Ty(ctx)       // waiting (set when thread is paused for GC)
    }, "GcThreadState");

    // GcRootFrame: per-function root frame pushed at entry, popped at exit
    // { i8* prev_frame, i32 count, [0 x AngaraObject*] }
    // The variable-length array of AngaraObject* pointers follows after count.
    // NOTE: we use a separate struct with a concrete array size at codegen time;
    // this zero-length type is only for the GC's traversal logic.
    m_gc_root_frame_type = StructType::create(ctx, {
        PointerType::get(ctx, 0),  // prev_frame
        Type::getInt32Ty(ctx),     // count
        ArrayType::get(PointerType::get(ctx, 0), 0)  // slots (placeholder)
    }, "GcRootFrame");
}

// ---------------------------------------------------------------------------
// Global variable generation
// ---------------------------------------------------------------------------

void MarkSweepGC::generateGlobals() {
    auto& ctx = m_ctx;

    // Head of the intrusive allocation linked list
    m_g_gc_head = new GlobalVariable(
        m_module, PointerType::get(ctx, 0),
        false, GlobalValue::InternalLinkage,
        ConstantPointerNull::get(PointerType::get(ctx, 0)),
        "__ang_gc_head");

    // Current number of live allocations
    m_g_gc_count = new GlobalVariable(
        m_module, Type::getInt64Ty(ctx),
        false, GlobalValue::InternalLinkage,
        ConstantInt::get(Type::getInt64Ty(ctx), 0),
        "__ang_gc_count");

    // Threshold: trigger collection when count >= threshold
    m_g_gc_threshold = new GlobalVariable(
        m_module, Type::getInt64Ty(ctx),
        false, GlobalValue::InternalLinkage,
        ConstantInt::get(Type::getInt64Ty(ctx), 1024),
        "__ang_gc_threshold");

    // Stop-the-world flag
    m_g_gc_running = new GlobalVariable(
        m_module, Type::getInt1Ty(ctx),
        false, GlobalValue::InternalLinkage,
        ConstantInt::getFalse(ctx),
        "__ang_gc_running");

    // Linked list of all registered thread states
    m_g_gc_threads = new GlobalVariable(
        m_module, PointerType::get(ctx, 0),
        false, GlobalValue::InternalLinkage,
        ConstantPointerNull::get(PointerType::get(ctx, 0)),
        "__ang_gc_threads");

    // Thread-local pointer to current thread's GcThreadState
    m_g_gc_thread_state_tls = new GlobalVariable(
        m_module, PointerType::get(ctx, 0),
        false, GlobalValue::InternalLinkage,
        ConstantPointerNull::get(PointerType::get(ctx, 0)),
        "__ang_gc_thread_state");
    // Mark as thread-local
    m_g_gc_thread_state_tls->setThreadLocal(true);
}

// ---------------------------------------------------------------------------
// Function generation
// ---------------------------------------------------------------------------

llvm::Function* MarkSweepGC::createRuntimeFunc(const std::string& name, llvm::FunctionType* type) {
    auto callee = m_module.getOrInsertFunction(name, type);
    auto* fn = cast<Function>(callee.getCallee());
    fn->setLinkage(Function::InternalLinkage);
    fn->setDSOLocal(true);
    return fn;
}

void MarkSweepGC::generateFunctions() {
    auto* void_ty = Type::getVoidTy(m_ctx);
    auto* i1_ty   = Type::getInt1Ty(m_ctx);
    auto* i8_ty   = Type::getInt8Ty(m_ctx);
    auto* i32_ty  = Type::getInt32Ty(m_ctx);
    auto* i64_ty  = Type::getInt64Ty(m_ctx);
    auto* i8_ptr  = PointerType::get(m_ctx, 0);
    auto* obj_ty  = m_angara_obj_type;
    auto* header_ty = m_obj_header_type;

    auto* malloc_fn = m_module.getFunction("malloc");
    auto* free_fn   = m_module.getFunction("free");

    // ===================================================================
    // __ang_gc_alloc(i64 size, i32 obj_type) -> i8*
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(i8_ptr, {i64_ty, i32_ty}, false);
        auto* fn = createRuntimeFunc("__ang_gc_alloc", fn_ty);
        m_fn_gc_alloc = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* check_bb = BasicBlock::Create(m_ctx, "check_threshold", fn);
        auto* alloc_bb = BasicBlock::Create(m_ctx, "do_alloc", fn);
        auto* done_bb  = BasicBlock::Create(m_ctx, "done", fn);

        IRBuilder<> b(entry);
        auto* size_arg = fn->arg_begin();
        auto* type_arg = fn->arg_begin() + 1;

        // Safepoint check: if GC is running, park this thread
        auto* running = b.CreateLoad(i1_ty, m_g_gc_running, "running");
        b.CreateCondBr(running, check_bb, alloc_bb);

        // In a full impl, check_bb would wait on a condition variable.
        // For now, just proceed — safepoint will be enhanced in Stage 5.
        IRBuilder<> bc(check_bb);
        bc.CreateBr(alloc_bb);

        // Allocate and initialize
        IRBuilder<> ba(alloc_bb);
        auto* mem = ba.CreateCall(malloc_fn, {size_arg}, "mem");
        auto* obj_ptr = ba.CreateBitCast(mem, i8_ptr, "obj_ptr");

        // Initialize header: type
        auto* type_addr = ba.CreateStructGEP(header_ty, obj_ptr, 0);
        ba.CreateStore(type_arg, type_addr);

        // Initialize header: meta = packMeta(WHITE, true) = (0 | (1 << 8)) = 256
        auto* meta_addr = ba.CreateStructGEP(header_ty, obj_ptr, 1);
        ba.CreateStore(ConstantInt::get(i32_ty, packMeta(COLOR_WHITE, true)), meta_addr);

        // Initialize header: next = current head
        auto* next_addr = ba.CreateStructGEP(header_ty, obj_ptr, 2);
        auto* old_head = ba.CreateLoad(i8_ptr, m_g_gc_head, "old_head");
        ba.CreateStore(old_head, next_addr);
        ba.CreateStore(obj_ptr, m_g_gc_head);

        // Increment count
        auto* count = ba.CreateLoad(i64_ty, m_g_gc_count, "count");
        auto* new_count = ba.CreateAdd(count, ConstantInt::get(i64_ty, 1), "new_count");
        ba.CreateStore(new_count, m_g_gc_count);

        // Check threshold
        auto* threshold = ba.CreateLoad(i64_ty, m_g_gc_threshold, "threshold");
        auto* over = ba.CreateICmpSGE(new_count, threshold, "over_threshold");
        ba.CreateCondBr(over, done_bb, done_bb); // Collection call added in Stage 4

        IRBuilder<> bd(done_bb);
        bd.CreateRet(obj_ptr);
    }

    // ===================================================================
    // __ang_gc_clear_unique(AngaraObject val) -> void
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_gc_clear_unique", fn_ty);
        m_fn_gc_clear_unique = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* is_obj_bb = BasicBlock::Create(m_ctx, "is_obj", fn);
        auto* done_bb = BasicBlock::Create(m_ctx, "done", fn);

        IRBuilder<> b(entry);
        auto* val = fn->arg_begin();
        auto* tag = b.CreateExtractValue(val, {0}, "tag");
        auto* is_obj = b.CreateICmpEQ(tag, ConstantInt::get(i32_ty, TAG_OBJ));
        b.CreateCondBr(is_obj, is_obj_bb, done_bb);

        IRBuilder<> b2(is_obj_bb);
        auto* payload = b2.CreateExtractValue(val, {1}, "payload");
        auto* ptr_i64 = b2.CreateBitCast(payload, i64_ty);
        auto* obj_ptr = b2.CreateIntToPtr(ptr_i64, i8_ptr, "obj_ptr");

        // Load meta, clear the is_unique bit (bit 8)
        auto* meta_addr = b2.CreateStructGEP(header_ty, obj_ptr, 1);
        auto* meta = b2.CreateLoad(i32_ty, meta_addr, "meta");
        auto* cleared = b2.CreateAnd(meta, ConstantInt::get(i32_ty, ~(1 << UNIQUE_SHIFT)), "cleared");
        b2.CreateStore(cleared, meta_addr);
        b2.CreateBr(done_bb);

        IRBuilder<> b3(done_bb);
        b3.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_mark_value(AngaraObject val) -> void
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_gc_mark_value", fn_ty);
        m_fn_gc_mark_value = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* is_obj_bb = BasicBlock::Create(m_ctx, "is_obj", fn);
        auto* done_bb = BasicBlock::Create(m_ctx, "done", fn);

        IRBuilder<> b(entry);
        auto* val = fn->arg_begin();
        auto* tag = b.CreateExtractValue(val, {0}, "tag");
        auto* is_obj = b.CreateICmpEQ(tag, ConstantInt::get(i32_ty, TAG_OBJ));
        b.CreateCondBr(is_obj, is_obj_bb, done_bb);

        IRBuilder<> b2(is_obj_bb);
        auto* payload = b2.CreateExtractValue(val, {1}, "payload");
        auto* ptr_i64 = b2.CreateBitCast(payload, i64_ty);
        auto* obj_ptr = b2.CreateIntToPtr(ptr_i64, i8_ptr, "obj_ptr");
        // Will call __ang_gc_mark in Stage 4
        b2.CreateBr(done_bb);

        IRBuilder<> b3(done_bb);
        b3.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_mark(i8* obj_ptr) -> void
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {i8_ptr}, false);
        auto* fn = createRuntimeFunc("__ang_gc_mark", fn_ty);
        m_fn_gc_mark = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        // Stub: will be implemented in Stage 4
        b.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_scan(i8* obj_ptr) -> void
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {i8_ptr}, false);
        auto* fn = createRuntimeFunc("__ang_gc_scan", fn_ty);
        m_fn_gc_scan = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        // Stub: will be implemented in Stage 4
        b.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_sweep() -> void
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {}, false);
        auto* fn = createRuntimeFunc("__ang_gc_sweep", fn_ty);
        m_fn_gc_sweep = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        // Stub: will be implemented in Stage 4
        b.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_finalize(i8* obj_ptr) -> void
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {i8_ptr}, false);
        auto* fn = createRuntimeFunc("__ang_gc_finalize", fn_ty);
        m_fn_gc_finalize = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        // Stub: will be implemented in Stage 4
        b.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_mark_roots() -> void
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {}, false);
        auto* fn = createRuntimeFunc("__ang_gc_mark_roots", fn_ty);
        m_fn_gc_mark_roots = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        // Stub: will be implemented in Stage 4/5
        b.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_collect() -> void
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {}, false);
        auto* fn = createRuntimeFunc("__ang_gc_collect", fn_ty);
        m_fn_gc_collect = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        // Stub: will be implemented in Stage 4
        b.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_push_frame(i8* frame_ptr) -> void
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {i8_ptr}, false);
        auto* fn = createRuntimeFunc("__ang_gc_push_frame", fn_ty);
        m_fn_gc_push_frame = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        // Stub: will be implemented in Stage 5
        b.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_pop_frame() -> void
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {}, false);
        auto* fn = createRuntimeFunc("__ang_gc_pop_frame", fn_ty);
        m_fn_gc_pop_frame = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        // Stub: will be implemented in Stage 5
        b.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_thread_register(i8* state_ptr) -> void
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {i8_ptr}, false);
        auto* fn = createRuntimeFunc("__ang_gc_thread_register", fn_ty);
        m_fn_gc_thread_register = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        // Stub: will be implemented in Stage 5
        b.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_thread_unregister(i8* state_ptr) -> void
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {i8_ptr}, false);
        auto* fn = createRuntimeFunc("__ang_gc_thread_unregister", fn_ty);
        m_fn_gc_thread_unregister = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        // Stub: will be implemented in Stage 5
        b.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_safepoint() -> void
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {}, false);
        auto* fn = createRuntimeFunc("__ang_gc_safepoint", fn_ty);
        m_fn_gc_safepoint = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        b.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_pin(AngaraObject val) -> void
    // For native module API — registers value as an explicit root.
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_gc_pin", fn_ty);
        m_fn_gc_pin = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        // Stub: will be implemented in Stage 6
        b.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_unpin(AngaraObject val) -> void
    // For native module API — unregisters an explicit root.
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_gc_unpin", fn_ty);
        m_fn_gc_unpin = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        // Stub: will be implemented in Stage 6
        b.CreateRetVoid();
    }
}

// ---------------------------------------------------------------------------
// Freestanding stubs
// ---------------------------------------------------------------------------

void MarkSweepGC::generateFreestandingStubs() {
    auto* void_ty = Type::getVoidTy(m_ctx);
    auto* i8_ptr  = PointerType::get(m_ctx, 0);
    auto* i32_ty  = Type::getInt32Ty(m_ctx);
    auto* i64_ty  = Type::getInt64Ty(m_ctx);
    auto* obj_ty  = m_angara_obj_type;

    auto stub_void = [&](const std::string& name, FunctionType* ty) {
        auto callee = m_module.getOrInsertFunction(name, ty);
        auto* fn = cast<Function>(callee.getCallee());
        fn->setLinkage(Function::InternalLinkage);
        fn->setDSOLocal(true);
        auto* e = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<>(e).CreateRetVoid();
    };

    // In freestanding mode, gc_alloc just calls malloc directly
    {
        auto callee = m_module.getOrInsertFunction("__ang_gc_alloc",
            FunctionType::get(i8_ptr, {i64_ty, i32_ty}, false));
        auto* fn = cast<Function>(callee.getCallee());
        fn->setLinkage(Function::InternalLinkage);
        fn->setDSOLocal(true);
        auto* e = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(e);
        auto* malloc_fn = m_module.getFunction("malloc");
        b.CreateRet(b.CreateCall(malloc_fn, {fn->arg_begin()}));
        m_fn_gc_alloc = FunctionCallee(fn);
    }

    stub_void("__ang_gc_collect", FunctionType::get(void_ty, {}, false));
    stub_void("__ang_gc_mark_value", FunctionType::get(void_ty, {obj_ty}, false));
    stub_void("__ang_gc_mark", FunctionType::get(void_ty, {i8_ptr}, false));
    stub_void("__ang_gc_scan", FunctionType::get(void_ty, {i8_ptr}, false));
    stub_void("__ang_gc_sweep", FunctionType::get(void_ty, {}, false));
    stub_void("__ang_gc_finalize", FunctionType::get(void_ty, {i8_ptr}, false));
    stub_void("__ang_gc_mark_roots", FunctionType::get(void_ty, {}, false));
    stub_void("__ang_gc_push_frame", FunctionType::get(void_ty, {i8_ptr}, false));
    stub_void("__ang_gc_pop_frame", FunctionType::get(void_ty, {}, false));
    stub_void("__ang_gc_thread_register", FunctionType::get(void_ty, {i8_ptr}, false));
    stub_void("__ang_gc_thread_unregister", FunctionType::get(void_ty, {i8_ptr}, false));
    stub_void("__ang_gc_safepoint", FunctionType::get(void_ty, {}, false));
    stub_void("__ang_gc_clear_unique", FunctionType::get(void_ty, {obj_ty}, false));
    stub_void("__ang_gc_pin", FunctionType::get(void_ty, {obj_ty}, false));
    stub_void("__ang_gc_unpin", FunctionType::get(void_ty, {obj_ty}, false));
}

} // namespace angara
