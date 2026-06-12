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
        ConstantInt::get(Type::getInt64Ty(ctx), 65536),
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
    m_g_gc_thread_state_tls->setThreadLocal(true);

    // Stats counters
    m_g_gc_collections = new GlobalVariable(
        m_module, Type::getInt64Ty(ctx),
        false, GlobalValue::InternalLinkage,
        ConstantInt::get(Type::getInt64Ty(ctx), 0),
        "__ang_gc_collections");

    m_g_gc_total_allocs = new GlobalVariable(
        m_module, Type::getInt64Ty(ctx),
        false, GlobalValue::InternalLinkage,
        ConstantInt::get(Type::getInt64Ty(ctx), 0),
        "__ang_gc_total_allocs");

    m_g_gc_total_frees = new GlobalVariable(
        m_module, Type::getInt64Ty(ctx),
        false, GlobalValue::InternalLinkage,
        ConstantInt::get(Type::getInt64Ty(ctx), 0),
        "__ang_gc_total_frees");

    m_g_gc_total_bytes_alloc = new GlobalVariable(
        m_module, Type::getInt64Ty(ctx),
        false, GlobalValue::InternalLinkage,
        ConstantInt::get(Type::getInt64Ty(ctx), 0),
        "__ang_gc_total_bytes_alloc");
}

// ---------------------------------------------------------------------------
// Function generation
// ---------------------------------------------------------------------------

llvm::Function* MarkSweepGC::createRuntimeFunc(const std::string& name, llvm::FunctionType* type) {
    auto callee = m_module.getOrInsertFunction(name, type);
    auto* fn = cast<Function>(callee.getCallee());
    fn->setLinkage(Function::InternalLinkage);
    fn->setDSOLocal(true);
    fn->addFnAttr(llvm::Attribute::NoInline);
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

    // Helper to look up a function by name for inter-function calls
    auto get_func = [&](const std::string& name) -> Function* {
        return m_module.getFunction(name);
    };

    // ===================================================================
    // Pre-declare mutually recursive functions:
    // mark_value -> mark -> scan -> mark_value
    // Also pre-declare collect (called from alloc before its body exists)
    // ===================================================================
    {
        auto* ty = FunctionType::get(void_ty, {obj_ty}, false);
        auto* fn = cast<Function>(m_module.getOrInsertFunction("__ang_gc_mark_value", ty).getCallee());
        fn->setLinkage(Function::InternalLinkage); fn->setDSOLocal(true);
    }
    {
        auto* ty = FunctionType::get(void_ty, {i8_ptr}, false);
        auto* fn = cast<Function>(m_module.getOrInsertFunction("__ang_gc_mark", ty).getCallee());
        fn->setLinkage(Function::InternalLinkage); fn->setDSOLocal(true);
    }
    {
        auto* ty = FunctionType::get(void_ty, {i8_ptr}, false);
        auto* fn = cast<Function>(m_module.getOrInsertFunction("__ang_gc_scan", ty).getCallee());
        fn->setLinkage(Function::InternalLinkage); fn->setDSOLocal(true);
    }
    {
        auto* ty = FunctionType::get(void_ty, {}, false);
        auto* fn = cast<Function>(m_module.getOrInsertFunction("__ang_gc_collect", ty).getCallee());
        fn->setLinkage(Function::InternalLinkage); fn->setDSOLocal(true);
    }
    {
        auto* ty = FunctionType::get(void_ty, {i8_ptr}, false);
        auto* fn = cast<Function>(m_module.getOrInsertFunction("__ang_gc_finalize", ty).getCallee());
        fn->setLinkage(Function::InternalLinkage); fn->setDSOLocal(true);
    }
    {
        auto* ty = FunctionType::get(void_ty, {}, false);
        auto* fn = cast<Function>(m_module.getOrInsertFunction("__ang_gc_sweep", ty).getCallee());
        fn->setLinkage(Function::InternalLinkage); fn->setDSOLocal(true);
    }
    {
        auto* ty = FunctionType::get(void_ty, {}, false);
        auto* fn = cast<Function>(m_module.getOrInsertFunction("__ang_gc_mark_roots", ty).getCallee());
        fn->setLinkage(Function::InternalLinkage); fn->setDSOLocal(true);
    }

    // ===================================================================
    // __ang_gc_alloc(i64 size, i32 obj_type) -> i8*
    // Wraps malloc, initializes header, threads onto allocation list,
    // and triggers collection when count >= threshold.
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(i8_ptr, {i64_ty, i32_ty}, false);
        auto* fn = createRuntimeFunc("__ang_gc_alloc", fn_ty);
        m_fn_gc_alloc = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* safepoint_bb = BasicBlock::Create(m_ctx, "safepoint", fn);
        auto* alloc_bb = BasicBlock::Create(m_ctx, "do_alloc", fn);
        auto* collect_bb = BasicBlock::Create(m_ctx, "collect", fn);
        auto* done_bb  = BasicBlock::Create(m_ctx, "done", fn);

        IRBuilder<> b(entry);
        auto* size_arg = fn->arg_begin();
        auto* type_arg = fn->arg_begin() + 1;

        // Safepoint check: if GC is running, set waiting and spin-wait
        auto* running = b.CreateLoad(i1_ty, m_g_gc_running, "running");
        b.CreateCondBr(running, safepoint_bb, alloc_bb);

        IRBuilder<> bsp(safepoint_bb);
        auto* tls_ptr = bsp.CreateLoad(i8_ptr, m_g_gc_thread_state_tls, "tls");
        auto* tls_ok = bsp.CreateICmpNE(tls_ptr, ConstantPointerNull::get(i8_ptr));
        auto* set_wait_bb = BasicBlock::Create(m_ctx, "set_wait", fn);
        auto* spin_bb = BasicBlock::Create(m_ctx, "spin", fn);
        bsp.CreateCondBr(tls_ok, set_wait_bb, spin_bb);

        IRBuilder<> bsw(set_wait_bb);
        bsw.CreateStore(ConstantInt::getTrue(m_ctx),
            bsw.CreateStructGEP(m_gc_thread_state_type, tls_ptr, 3));
        bsw.CreateBr(spin_bb);

        IRBuilder<> bspin(spin_bb);
        auto* still_running = bspin.CreateLoad(i1_ty, m_g_gc_running, "still_running");
        auto* clear_wait_bb = BasicBlock::Create(m_ctx, "clear_wait", fn);
        bspin.CreateCondBr(still_running, spin_bb, clear_wait_bb);

        IRBuilder<> bcw(clear_wait_bb);
        auto* tls2 = bcw.CreateLoad(i8_ptr, m_g_gc_thread_state_tls, "tls2");
        auto* tls2_ok = bcw.CreateICmpNE(tls2, ConstantPointerNull::get(i8_ptr));
        auto* do_clear_bb = BasicBlock::Create(m_ctx, "do_clear", fn);
        bcw.CreateCondBr(tls2_ok, do_clear_bb, alloc_bb);

        IRBuilder<> bdc(do_clear_bb);
        bdc.CreateStore(ConstantInt::getFalse(m_ctx),
            bdc.CreateStructGEP(m_gc_thread_state_type, tls2, 3));
        bdc.CreateBr(alloc_bb);

        // Allocate and initialize
        IRBuilder<> ba(alloc_bb);
        auto* mem = ba.CreateCall(malloc_fn, {size_arg}, "mem");

        // Initialize header: type
        auto* type_addr = ba.CreateStructGEP(header_ty, mem, 0);
        ba.CreateStore(type_arg, type_addr);

        // Initialize header: meta = packMeta(WHITE, true)
        auto* meta_addr = ba.CreateStructGEP(header_ty, mem, 1);
        ba.CreateStore(ConstantInt::get(i32_ty, packMeta(COLOR_WHITE, true)), meta_addr);

        // Initialize header: next = null (will be linked after collection check)
        auto* next_addr = ba.CreateStructGEP(header_ty, mem, 2);
        ba.CreateStore(ConstantPointerNull::get(i8_ptr), next_addr);

        // Increment count
        auto* count = ba.CreateLoad(i64_ty, m_g_gc_count, "count");
        auto* new_count = ba.CreateAdd(count, ConstantInt::get(i64_ty, 1), "new_count");
        ba.CreateStore(new_count, m_g_gc_count);

        // Stats: increment total_allocs and total_bytes_alloc
        auto* total_allocs = ba.CreateLoad(i64_ty, m_g_gc_total_allocs, "total_allocs");
        ba.CreateStore(ba.CreateAdd(total_allocs, ConstantInt::get(i64_ty, 1)), m_g_gc_total_allocs);
        auto* total_bytes = ba.CreateLoad(i64_ty, m_g_gc_total_bytes_alloc, "total_bytes");
        ba.CreateStore(ba.CreateAdd(total_bytes, size_arg), m_g_gc_total_bytes_alloc);

        // Check threshold BEFORE linking — so the collector doesn't see
        // this object with uninitialized type-specific fields
        auto* threshold = ba.CreateLoad(i64_ty, m_g_gc_threshold, "threshold");
        auto* over = ba.CreateICmpSGE(new_count, threshold, "over_threshold");
        ba.CreateCondBr(over, collect_bb, done_bb);

        // Collect if over threshold
        IRBuilder<> bc(collect_bb);
        bc.CreateCall(get_func("__ang_gc_collect"), {});
        bc.CreateBr(done_bb);

        // Link into allocation list AFTER potential collection
        IRBuilder<> bd(done_bb);
        auto* old_head = bd.CreateLoad(i8_ptr, m_g_gc_head, "old_head");
        bd.CreateStore(old_head, next_addr);
        bd.CreateStore(mem, m_g_gc_head);
        bd.CreateRet(mem);
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

        auto* meta_gaddr = b2.CreateStructGEP(header_ty, obj_ptr, 1);
        auto* meta = b2.CreateLoad(i32_ty, meta_gaddr, "meta");
        auto* cleared = b2.CreateAnd(meta, ConstantInt::get(i32_ty, ~(1 << UNIQUE_SHIFT)), "cleared");
        b2.CreateStore(cleared, meta_gaddr);
        b2.CreateBr(done_bb);

        IRBuilder<> b3(done_bb);
        b3.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_mark_value(AngaraObject val) -> void
    // If tag == TAG_OBJ, extract payload pointer and call __ang_gc_mark.
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
        b2.CreateCall(get_func("__ang_gc_mark"), {obj_ptr});
        b2.CreateBr(done_bb);

        IRBuilder<> b3(done_bb);
        b3.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_mark(i8* obj_ptr) -> void
    // Tri-color marking: if not BLACK, set GRAY, scan children, set BLACK.
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {i8_ptr}, false);
        auto* fn = createRuntimeFunc("__ang_gc_mark", fn_ty);
        m_fn_gc_mark = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* not_black_bb = BasicBlock::Create(m_ctx, "not_black", fn);
        auto* scan_bb = BasicBlock::Create(m_ctx, "scan_bb", fn);
        auto* done_bb = BasicBlock::Create(m_ctx, "done", fn);

        IRBuilder<> b(entry);
        auto* obj_ptr = fn->arg_begin();

        // Load color from meta field
        auto* meta_gaddr = b.CreateStructGEP(header_ty, obj_ptr, 1);
        auto* meta = b.CreateLoad(i32_ty, meta_gaddr, "meta");
        auto* color = b.CreateAnd(meta, ConstantInt::get(i32_ty, 0xFF), "color");
        auto* is_black = b.CreateICmpEQ(color, ConstantInt::get(i32_ty, COLOR_BLACK));
        b.CreateCondBr(is_black, done_bb, not_black_bb);

        // Not black: set to GRAY
        IRBuilder<>bnb(not_black_bb);
        auto* gray_meta = bnb.CreateOr(
            bnb.CreateAnd(meta, ConstantInt::get(i32_ty, ~0xFF)),
            ConstantInt::get(i32_ty, COLOR_GRAY), "gray_meta");
        bnb.CreateStore(gray_meta, bnb.CreateStructGEP(header_ty, obj_ptr, 1));
        bnb.CreateBr(scan_bb);

        // Scan children
        IRBuilder<>bs(scan_bb);
        bs.CreateCall(get_func("__ang_gc_scan"), {obj_ptr});

        // Set to BLACK
        auto* black_meta = bs.CreateOr(
            bs.CreateAnd(meta, ConstantInt::get(i32_ty, ~0xFF)),
            ConstantInt::get(i32_ty, COLOR_BLACK), "black_meta");
        // Re-load meta in case scan modified it (it shouldn't, but for correctness)
        auto* meta2 = bs.CreateLoad(i32_ty, meta_gaddr, "meta2");
        auto* black_meta2 = bs.CreateOr(
            bs.CreateAnd(meta2, ConstantInt::get(i32_ty, ~0xFF)),
            ConstantInt::get(i32_ty, COLOR_BLACK), "black_meta2");
        bs.CreateStore(black_meta2, meta_gaddr);
        bs.CreateBr(done_bb);

        IRBuilder<> bd(done_bb);
        bd.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_scan(i8* obj_ptr) -> void
    // Type-dispatched traversal: switch on ObjHeader.type, mark children.
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {i8_ptr}, false);
        auto* fn = createRuntimeFunc("__ang_gc_scan", fn_ty);
        m_fn_gc_scan = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* obj_ptr = fn->arg_begin();

        auto* type_gaddr = b.CreateStructGEP(header_ty, obj_ptr, 0);
        auto* obj_type = b.CreateLoad(i32_ty, type_gaddr, "obj_type");

        // Create basic blocks for each object type
        auto* string_bb = BasicBlock::Create(m_ctx, "scan_string", fn);
        auto* list_bb = BasicBlock::Create(m_ctx, "scan_list", fn);
        auto* record_bb = BasicBlock::Create(m_ctx, "scan_record", fn);
        auto* exception_bb = BasicBlock::Create(m_ctx, "scan_exception", fn);
        auto* thread_bb = BasicBlock::Create(m_ctx, "scan_thread", fn);
        auto* mutex_bb = BasicBlock::Create(m_ctx, "scan_mutex", fn);
        auto* closure_bb = BasicBlock::Create(m_ctx, "scan_closure", fn);
        auto* native_bb = BasicBlock::Create(m_ctx, "scan_native", fn);
        auto* bound_bb = BasicBlock::Create(m_ctx, "scan_bound", fn);
        auto* done_bb = BasicBlock::Create(m_ctx, "done", fn);

        auto* sw = b.CreateSwitch(obj_type, done_bb, 13);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_STRING), string_bb);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_LIST), list_bb);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_RECORD), record_bb);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_EXCEPTION), exception_bb);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_THREAD), thread_bb);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_MUTEX), mutex_bb);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_CLOSURE), closure_bb);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_CLASS), record_bb);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_INSTANCE), record_bb);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_NATIVE_INSTANCE), native_bb);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_DATA_INSTANCE), record_bb);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_ENUM_INSTANCE), record_bb);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_BOUND_METHOD), bound_bb);

        // OBJ_STRING: no managed children (chars is raw C buffer)
        {
            IRBuilder<> bs(string_bb);
            bs.CreateBr(done_bb);
        }

        // OBJ_MUTEX: no managed children
        {
            IRBuilder<> bm(mutex_bb);
            bm.CreateBr(done_bb);
        }

        // OBJ_NATIVE_INSTANCE: no managed children (opaque data)
        {
            IRBuilder<> bn(native_bb);
            bn.CreateBr(done_bb);
        }

        // OBJ_LIST: iterate elements, mark each
        // AngaraList: { ObjHeader, i64 count, i64 cap, AngaraObject* elems }
        {
            // list_bb is the switch case target — load fields then branch to loop
            IRBuilder<> bls(list_bb);
            auto* count = bls.CreateLoad(i64_ty,
                bls.CreateStructGEP(m_list_type, obj_ptr, 1), "count");
            auto* elems = bls.CreateLoad(i8_ptr,
                bls.CreateStructGEP(m_list_type, obj_ptr, 3), "elems");
            auto* list_loop_bb = BasicBlock::Create(m_ctx, "list_loop", fn);
            bls.CreateBr(list_loop_bb);

            IRBuilder<> bll(list_loop_bb);
            auto* i_phi = bll.CreatePHI(i64_ty, 2, "i");
            i_phi->addIncoming(ConstantInt::get(i64_ty, 0), list_bb);
            auto* cont = bll.CreateICmpSLT(i_phi, count);
            auto* list_body_bb = BasicBlock::Create(m_ctx, "list_body", fn);
            auto* list_done_bb = BasicBlock::Create(m_ctx, "list_done", fn);
            bll.CreateCondBr(cont, list_body_bb, list_done_bb);

            IRBuilder<> bb(list_body_bb);
            auto* elem_ptr = bb.CreateGEP(obj_ty, elems, {i_phi});
            auto* elem = bb.CreateLoad(obj_ty, elem_ptr, "elem");
            bb.CreateCall(get_func("__ang_gc_mark_value"), {elem});
            auto* next_i = bb.CreateAdd(i_phi, ConstantInt::get(i64_ty, 1));
            bb.CreateBr(list_loop_bb);
            i_phi->addIncoming(next_i, list_body_bb);

            IRBuilder<> bld(list_done_bb);
            bld.CreateBr(done_bb);
        }

        // OBJ_EXCEPTION: mark the message
        // AngaraException: { ObjHeader, AngaraObject message }
        {
            IRBuilder<> be(exception_bb);
            auto* msg = be.CreateLoad(obj_ty,
                be.CreateStructGEP(m_exception_type, obj_ptr, 1), "msg");
            be.CreateCall(get_func("__ang_gc_mark_value"), {msg});
            be.CreateBr(done_bb);
        }

        // OBJ_CLOSURE: iterate env array, mark each captured variable
        // AngaraClosure: { ObjHeader, i8* fn, i32 arity, i1 native, i8* env, i32 env_count }
        {
            auto* env_loop_bb = BasicBlock::Create(m_ctx, "env_loop", fn);
            auto* env_body_bb = BasicBlock::Create(m_ctx, "env_body", fn);
            auto* env_done_bb = BasicBlock::Create(m_ctx, "env_done", fn);

            IRBuilder<> bc(closure_bb);
            auto* env = bc.CreateLoad(i8_ptr,
                bc.CreateStructGEP(m_closure_type, obj_ptr, 4), "env");
            auto* env_count = bc.CreateLoad(i32_ty,
                bc.CreateStructGEP(m_closure_type, obj_ptr, 5), "env_count");
            auto* env_not_null = bc.CreateICmpNE(env, ConstantPointerNull::get(i8_ptr));
            auto* count_pos = bc.CreateICmpSGT(env_count, ConstantInt::get(i32_ty, 0));
            auto* has_env = bc.CreateAnd(env_not_null, count_pos);
            bc.CreateCondBr(has_env, env_loop_bb, env_done_bb);

            IRBuilder<> bel(env_loop_bb);
            auto* i_phi = bel.CreatePHI(i64_ty, 2, "i");
            i_phi->addIncoming(ConstantInt::get(i64_ty, 0), closure_bb);
            auto* env_count_ext = bel.CreateZExt(env_count, i64_ty, "env_count_ext");
            auto* cont = bel.CreateICmpSLT(i_phi, env_count_ext);
            bel.CreateCondBr(cont, env_body_bb, env_done_bb);

            IRBuilder<> beb(env_body_bb);
            auto* elem_ptr = beb.CreateGEP(obj_ty, env, {i_phi});
            auto* elem = beb.CreateLoad(obj_ty, elem_ptr, "elem");
            beb.CreateCall(get_func("__ang_gc_mark_value"), {elem});
            auto* next_i = beb.CreateAdd(i_phi, ConstantInt::get(i64_ty, 1));
            beb.CreateBr(env_loop_bb);
            i_phi->addIncoming(next_i, env_body_bb);

            IRBuilder<> bed(env_done_bb);
            bed.CreateBr(done_bb);
        }

        // OBJ_BOUND_METHOD: mark receiver and method
        // AngaraBoundMethod: { ObjHeader, AngaraObject receiver, AngaraObject method }
        {
            IRBuilder<> bb(bound_bb);
            auto* receiver = bb.CreateLoad(obj_ty,
                bb.CreateStructGEP(m_bound_method_type, obj_ptr, 1), "receiver");
            bb.CreateCall(get_func("__ang_gc_mark_value"), {receiver});
            auto* method = bb.CreateLoad(obj_ty,
                bb.CreateStructGEP(m_bound_method_type, obj_ptr, 2), "method");
            bb.CreateCall(get_func("__ang_gc_mark_value"), {method});
            bb.CreateBr(done_bb);
        }

        // OBJ_THREAD: mark closure + args array
        // AngaraThread: { ObjHeader, i8* pthread, AngaraObject closure, i32 argc, i8* args }
        {
            auto* thread_done_bb = BasicBlock::Create(m_ctx, "thread_done", fn);
            auto* args_loop_bb = BasicBlock::Create(m_ctx, "args_loop", fn);
            auto* args_body_bb = BasicBlock::Create(m_ctx, "args_body", fn);
            auto* args_done_bb = BasicBlock::Create(m_ctx, "args_done", fn);

            IRBuilder<> bt(thread_bb);
            // Mark the closure
            auto* closure = bt.CreateLoad(obj_ty,
                bt.CreateStructGEP(m_thread_type, obj_ptr, 2), "closure");
            bt.CreateCall(get_func("__ang_gc_mark_value"), {closure});

            // Mark args array if present
            auto* argc = bt.CreateLoad(i32_ty,
                bt.CreateStructGEP(m_thread_type, obj_ptr, 3), "argc");
            auto* args = bt.CreateLoad(i8_ptr,
                bt.CreateStructGEP(m_thread_type, obj_ptr, 4), "args");
            auto* args_not_null = bt.CreateICmpNE(args, ConstantPointerNull::get(i8_ptr));
            auto* argc_pos = bt.CreateICmpSGT(argc, ConstantInt::get(i32_ty, 0));
            auto* has_args = bt.CreateAnd(args_not_null, argc_pos);
            bt.CreateCondBr(has_args, args_loop_bb, thread_done_bb);

            IRBuilder<> bal(args_loop_bb);
            auto* i_phi = bal.CreatePHI(i64_ty, 2, "i");
            i_phi->addIncoming(ConstantInt::get(i64_ty, 0), thread_bb);
            auto* argc_ext = bal.CreateZExt(argc, i64_ty, "argc_ext");
            auto* cont = bal.CreateICmpSLT(i_phi, argc_ext);
            bal.CreateCondBr(cont, args_body_bb, args_done_bb);

            IRBuilder<> bab(args_body_bb);
            auto* elem_ptr = bab.CreateGEP(obj_ty, args, {i_phi});
            auto* elem = bab.CreateLoad(obj_ty, elem_ptr, "elem");
            bab.CreateCall(get_func("__ang_gc_mark_value"), {elem});
            auto* next_i = bab.CreateAdd(i_phi, ConstantInt::get(i64_ty, 1));
            bab.CreateBr(args_loop_bb);
            i_phi->addIncoming(next_i, args_body_bb);

            IRBuilder<> bad(args_done_bb);
            bad.CreateBr(thread_done_bb);

            IRBuilder<> btd(thread_done_bb);
            btd.CreateBr(done_bb);
        }

        // OBJ_RECORD (also OBJ_CLASS, OBJ_INSTANCE, OBJ_DATA_INSTANCE, OBJ_ENUM_INSTANCE):
        // iterate entries, mark each value
        // AngaraRecord: { ObjHeader, i64 count, i64 cap, RecordEntry* entries }
        // RecordEntry: { i8* key, AngaraObject value }
        {
            auto* rec_loop_bb = BasicBlock::Create(m_ctx, "rec_loop", fn);
            auto* rec_body_bb = BasicBlock::Create(m_ctx, "rec_body", fn);
            auto* rec_done_bb = BasicBlock::Create(m_ctx, "rec_done", fn);

            IRBuilder<> br(record_bb);
            auto* count = br.CreateLoad(i64_ty,
                br.CreateStructGEP(m_record_type, obj_ptr, 1), "count");
            auto* entries = br.CreateLoad(i8_ptr,
                br.CreateStructGEP(m_record_type, obj_ptr, 3), "entries");
            br.CreateBr(rec_loop_bb);

            IRBuilder<> brl(rec_loop_bb);
            auto* i_phi = brl.CreatePHI(i64_ty, 2, "i");
            i_phi->addIncoming(ConstantInt::get(i64_ty, 0), record_bb);
            auto* cont = brl.CreateICmpSLT(i_phi, count);
            brl.CreateCondBr(cont, rec_body_bb, rec_done_bb);

            IRBuilder<> brb(rec_body_bb);
            auto* entry_ptr = brb.CreateGEP(m_record_entry_type, entries, {i_phi});
            auto* val = brb.CreateLoad(obj_ty,
                brb.CreateStructGEP(m_record_entry_type, entry_ptr, 1), "val");
            brb.CreateCall(get_func("__ang_gc_mark_value"), {val});
            auto* next_i = brb.CreateAdd(i_phi, ConstantInt::get(i64_ty, 1));
            brb.CreateBr(rec_loop_bb);
            i_phi->addIncoming(next_i, rec_body_bb);

            IRBuilder<> brd(rec_done_bb);
            brd.CreateBr(done_bb);
        }

        // Default done block
        IRBuilder<> bd(done_bb);
        bd.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_finalize(i8* obj_ptr) -> void
    // Free internal buffers before sweep frees the struct.
    // Handles: OBJ_STRING (chars), OBJ_LIST (elements), OBJ_RECORD (entries+keys),
    //          OBJ_NATIVE_INSTANCE (finalize callback).
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {i8_ptr}, false);
        auto* fn = createRuntimeFunc("__ang_gc_finalize", fn_ty);
        m_fn_gc_finalize = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* string_bb = BasicBlock::Create(m_ctx, "string", fn);
        auto* list_bb = BasicBlock::Create(m_ctx, "list", fn);
        auto* record_bb = BasicBlock::Create(m_ctx, "record", fn);
        auto* record_loop_bb = BasicBlock::Create(m_ctx, "rec_loop", fn);
        auto* record_body_bb = BasicBlock::Create(m_ctx, "rec_body", fn);
        auto* record_free_entries_bb = BasicBlock::Create(m_ctx, "rec_free_entries", fn);
        auto* native_bb = BasicBlock::Create(m_ctx, "native", fn);
        auto* call_finalize_bb = BasicBlock::Create(m_ctx, "call_finalize", fn);
        auto* done_bb = BasicBlock::Create(m_ctx, "done", fn);

        IRBuilder<> b(entry);
        auto* obj_ptr = fn->arg_begin();
        auto* type_gaddr = b.CreateStructGEP(header_ty, obj_ptr, 0);
        auto* obj_type = b.CreateLoad(i32_ty, type_gaddr, "obj_type");

        auto* sw = b.CreateSwitch(obj_type, done_bb, 4);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_STRING), string_bb);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_LIST), list_bb);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_RECORD), record_bb);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_NATIVE_INSTANCE), native_bb);

        // OBJ_STRING: free chars buffer (field 3 of AngaraString)
        {
            IRBuilder<> bs(string_bb);
            auto* chars_ptr = bs.CreateLoad(i8_ptr,
                bs.CreateStructGEP(m_string_type, obj_ptr, 3), "chars");
            auto* has_chars = bs.CreateICmpNE(chars_ptr,
                ConstantPointerNull::get(i8_ptr));
            auto* free_chars_bb = BasicBlock::Create(m_ctx, "free_chars", fn);
            bs.CreateCondBr(has_chars, free_chars_bb, done_bb);

            IRBuilder<> bfc(free_chars_bb);
            bfc.CreateCall(free_fn, {chars_ptr});
            bfc.CreateBr(done_bb);
        }

        // OBJ_LIST: free elements array (field 3 of AngaraList)
        {
            IRBuilder<> bl(list_bb);
            auto* elems_ptr = bl.CreateLoad(PointerType::get(m_ctx, 0),
                bl.CreateStructGEP(m_list_type, obj_ptr, 3), "elems");
            auto* has_elems = bl.CreateICmpNE(elems_ptr,
                ConstantPointerNull::get(PointerType::get(m_ctx, 0)));
            auto* free_elems_bb = BasicBlock::Create(m_ctx, "free_elems", fn);
            bl.CreateCondBr(has_elems, free_elems_bb, done_bb);

            IRBuilder<> bfe(free_elems_bb);
            auto* elems_raw = bfe.CreateBitCast(elems_ptr, i8_ptr);
            bfe.CreateCall(free_fn, {elems_raw});
            bfe.CreateBr(done_bb);
        }

        // OBJ_RECORD: free each strdup'd key, then free entries array
        {
            IRBuilder<> br(record_bb);
            auto* count = br.CreateLoad(i64_ty,
                br.CreateStructGEP(m_record_type, obj_ptr, 1), "count");
            auto* entries = br.CreateLoad(PointerType::get(m_ctx, 0),
                br.CreateStructGEP(m_record_type, obj_ptr, 3), "entries");
            auto* has_entries = br.CreateICmpNE(entries,
                ConstantPointerNull::get(PointerType::get(m_ctx, 0)));
            br.CreateCondBr(has_entries, record_loop_bb, done_bb);

            IRBuilder<> brl(record_loop_bb);
            auto* i_phi = brl.CreatePHI(i64_ty, 2, "i");
            i_phi->addIncoming(ConstantInt::get(i64_ty, 0), record_bb);
            auto* cont = brl.CreateICmpSLT(i_phi, count);
            brl.CreateCondBr(cont, record_body_bb, record_free_entries_bb);

            IRBuilder<> brb(record_body_bb);
            auto* entry_ptr = brb.CreateGEP(m_record_entry_type, entries, {i_phi});
            auto* key = brb.CreateLoad(i8_ptr,
                brb.CreateStructGEP(m_record_entry_type, entry_ptr, 0), "key");
            brb.CreateCall(free_fn, {key});  // free(NULL) is safe
            auto* next_i = brb.CreateAdd(i_phi, ConstantInt::get(i64_ty, 1));
            brb.CreateBr(record_loop_bb);
            i_phi->addIncoming(next_i, record_body_bb);

            IRBuilder<> brfe(record_free_entries_bb);
            auto* entries_raw = brfe.CreateBitCast(entries, i8_ptr);
            brfe.CreateCall(free_fn, {entries_raw});
            brfe.CreateBr(done_bb);
        }

        // OBJ_NATIVE_INSTANCE: call the finalize function pointer
        {
            IRBuilder<> bn(native_bb);
            auto* finalize_fn = bn.CreateLoad(i8_ptr,
                bn.CreateStructGEP(m_native_instance_type, obj_ptr, 2), "finalize_fn");
            auto* has_fn = bn.CreateICmpNE(finalize_fn, ConstantPointerNull::get(i8_ptr));
            bn.CreateCondBr(has_fn, call_finalize_bb, done_bb);

            IRBuilder<> bcf(call_finalize_bb);
            auto* data = bcf.CreateLoad(i8_ptr,
                bcf.CreateStructGEP(m_native_instance_type, obj_ptr, 1), "data");
            auto* finalize_ty = FunctionType::get(void_ty, {i8_ptr}, false);
            bcf.CreateCall(finalize_ty, finalize_fn, {data});
            bcf.CreateBr(done_bb);
        }

        IRBuilder<> bd(done_bb);
        bd.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_sweep() -> void
    // Walk the allocation list. Free WHITE objects, reset BLACK to WHITE.
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {}, false);
        auto* fn = createRuntimeFunc("__ang_gc_sweep", fn_ty);
        m_fn_gc_sweep = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* loop_bb = BasicBlock::Create(m_ctx, "loop", fn);
        auto* check_color_bb = BasicBlock::Create(m_ctx, "check_color", fn);
        auto* is_white_bb = BasicBlock::Create(m_ctx, "is_white", fn);
        auto* check_pinned_bb = BasicBlock::Create(m_ctx, "check_pinned", fn);
        auto* unlink_head_bb = BasicBlock::Create(m_ctx, "unlink_head", fn);
        auto* unlink_mid_bb = BasicBlock::Create(m_ctx, "unlink_mid", fn);
        auto* free_obj_bb = BasicBlock::Create(m_ctx, "free_obj", fn);
        auto* is_black_bb = BasicBlock::Create(m_ctx, "is_black", fn);
        auto* advance_bb = BasicBlock::Create(m_ctx, "advance", fn);
        auto* done_bb = BasicBlock::Create(m_ctx, "done", fn);

        IRBuilder<> b(entry);
        // prev starts as null (no previous node)
        auto* head = b.CreateLoad(i8_ptr, m_g_gc_head, "head");
        b.CreateBr(loop_bb);

        // Loop: curr = phi [head, entry], [next, advance]
        //        prev = phi [null, entry], [prev_next, advance]
        IRBuilder<> bl(loop_bb);
        auto* curr_phi = bl.CreatePHI(i8_ptr, 2, "curr");
        curr_phi->addIncoming(head, entry);
        auto* prev_phi = bl.CreatePHI(i8_ptr, 2, "prev");
        prev_phi->addIncoming(ConstantPointerNull::get(i8_ptr), entry);
        auto* is_null = bl.CreateICmpEQ(curr_phi, ConstantPointerNull::get(i8_ptr));
        bl.CreateCondBr(is_null, done_bb, check_color_bb);

        // Check color
        IRBuilder<> bcc(check_color_bb);
        auto* meta_gaddr = bcc.CreateStructGEP(header_ty, curr_phi, 1);
        auto* meta = bcc.CreateLoad(i32_ty, meta_gaddr, "meta");
        auto* color = bcc.CreateAnd(meta, ConstantInt::get(i32_ty, 0xFF), "color");
        auto* is_white = bcc.CreateICmpEQ(color, ConstantInt::get(i32_ty, COLOR_WHITE));
        bcc.CreateCondBr(is_white, is_white_bb, is_black_bb);

        // WHITE: check if pinned before freeing
        IRBuilder<> bw(is_white_bb);
        auto* pinned_bit = bw.CreateAnd(meta, ConstantInt::get(i32_ty, 1 << 16), "pinned");
        auto* is_pinned = bw.CreateICmpNE(pinned_bit, ConstantInt::get(i32_ty, 0));
        bw.CreateCondBr(is_pinned, is_black_bb, check_pinned_bb);

        // Not pinned WHITE: unlink and free
        IRBuilder<> bcp(check_pinned_bb);
        // Get next pointer before we free
        auto* next_ptr = bcp.CreateLoad(i8_ptr,
            bcp.CreateStructGEP(header_ty, curr_phi, 2), "next");
        // Check if prev is null (head of list)
        auto* prev_is_null = bcp.CreateICmpEQ(prev_phi, ConstantPointerNull::get(i8_ptr));
        bcp.CreateCondBr(prev_is_null, unlink_head_bb, unlink_mid_bb);

        // Unlink from head
        IRBuilder<> buh(unlink_head_bb);
        buh.CreateStore(next_ptr, m_g_gc_head);
        buh.CreateBr(free_obj_bb);

        // Unlink from middle
        IRBuilder<> bum(unlink_mid_bb);
        bum.CreateStore(next_ptr,
            bum.CreateStructGEP(header_ty, prev_phi, 2));
        bum.CreateBr(free_obj_bb);

        // Free the object
        IRBuilder<> bf(free_obj_bb);
        bf.CreateCall(get_func("__ang_gc_finalize"), {curr_phi});
        bf.CreateCall(free_fn, {curr_phi});
        // Stats: increment total_frees
        auto* frees = bf.CreateLoad(i64_ty, m_g_gc_total_frees, "frees");
        bf.CreateStore(bf.CreateAdd(frees, ConstantInt::get(i64_ty, 1)), m_g_gc_total_frees);
        bf.CreateBr(advance_bb);

        // BLACK: reset color to WHITE, preserve pinned/unique bits, advance prev
        IRBuilder<> bbk(is_black_bb);
        auto* next_ptr2 = bbk.CreateLoad(i8_ptr,
            bbk.CreateStructGEP(header_ty, curr_phi, 2), "next2");
        // Clear color bits (byte 0) to WHITE, keep all other bits (pinned, unique, etc.)
        auto* reset_meta = bbk.CreateAnd(meta, ConstantInt::get(i32_ty, ~0xFF), "reset_meta");
        bbk.CreateStore(reset_meta, bbk.CreateStructGEP(header_ty, curr_phi, 1));
        bbk.CreateBr(advance_bb);

        // Advance: move to next node
        IRBuilder<> ba(advance_bb);
        // next depends on whether we freed or kept
        auto* next_phi = ba.CreatePHI(i8_ptr, 2, "next");
        next_phi->addIncoming(next_ptr, free_obj_bb);     // after free: use next from white path
        next_phi->addIncoming(next_ptr2, is_black_bb);    // after keep: use next from black path
        // prev depends on whether we freed or kept
        auto* prev_next = ba.CreatePHI(i8_ptr, 2, "prev_next");
        prev_next->addIncoming(prev_phi, free_obj_bb);    // after free: prev stays same
        prev_next->addIncoming(curr_phi, is_black_bb);    // after keep: prev = curr
        curr_phi->addIncoming(next_phi, advance_bb);
        prev_phi->addIncoming(prev_next, advance_bb);
        ba.CreateBr(loop_bb);

        // Done: update count (sweep already tracks dead via free)
        IRBuilder<> bd(done_bb);
        // After sweeping, surviving objects are reset to WHITE.
        // Count the survivors by walking the list... or just set count = 0 and recount.
        // Simpler: walk and count. But that's another loop. For now, just don't update count here.
        // The count will be adjusted in collect.
        bd.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_mark_roots() -> void
    // Walk thread list -> walk root frames -> mark values.
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {}, false);
        auto* fn = createRuntimeFunc("__ang_gc_mark_roots", fn_ty);
        m_fn_gc_mark_roots = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* thread_loop_bb = BasicBlock::Create(m_ctx, "thread_loop", fn);
        auto* frame_setup_bb = BasicBlock::Create(m_ctx, "frame_setup", fn);
        auto* frame_loop_bb = BasicBlock::Create(m_ctx, "frame_loop", fn);
        auto* slot_loop_bb = BasicBlock::Create(m_ctx, "slot_loop", fn);
        auto* slot_body_bb = BasicBlock::Create(m_ctx, "slot_body", fn);
        auto* next_frame_bb = BasicBlock::Create(m_ctx, "next_frame", fn);
        auto* next_thread_bb = BasicBlock::Create(m_ctx, "next_thread", fn);
        auto* done_bb = BasicBlock::Create(m_ctx, "done", fn);

        IRBuilder<> b(entry);
        auto* threads = b.CreateLoad(i8_ptr, m_g_gc_threads, "threads");
        b.CreateBr(thread_loop_bb);

        // Thread loop: iterate linked list of GcThreadState
        IRBuilder<> btl(thread_loop_bb);
        auto* thread_phi = btl.CreatePHI(i8_ptr, 2, "thread");
        thread_phi->addIncoming(threads, entry);
        auto* thread_done = btl.CreateICmpEQ(thread_phi, ConstantPointerNull::get(i8_ptr));
        btl.CreateCondBr(thread_done, done_bb, frame_setup_bb);

        // Frame setup: load root_frames for this thread, branch to frame_loop
        IRBuilder<> bfs(frame_setup_bb);
        auto* root_frames = bfs.CreateLoad(i8_ptr,
            bfs.CreateStructGEP(m_gc_thread_state_type, thread_phi, 2), "root_frames");
        bfs.CreateBr(frame_loop_bb);

        // Frame loop: PHI on frame pointer
        IRBuilder<> bfl(frame_loop_bb);
        auto* frame_phi = bfl.CreatePHI(i8_ptr, 2, "frame");
        frame_phi->addIncoming(root_frames, frame_setup_bb);
        auto* frame_done = bfl.CreateICmpEQ(frame_phi, ConstantPointerNull::get(i8_ptr));
        bfl.CreateCondBr(frame_done, next_thread_bb, slot_loop_bb);

        // Slot loop: iterate slots of this frame
        IRBuilder<> bsl(slot_loop_bb);
        auto* i_phi = bsl.CreatePHI(i64_ty, 2, "slot_i");
        i_phi->addIncoming(ConstantInt::get(i64_ty, 0), frame_loop_bb);
        auto* count = bsl.CreateLoad(i32_ty,
            bsl.CreateStructGEP(m_gc_root_frame_type, frame_phi, 1), "slot_count");
        auto* count_ext = bsl.CreateZExt(count, i64_ty, "count_ext");
        auto* has_more = bsl.CreateICmpSLT(i_phi, count_ext);
        bsl.CreateCondBr(has_more, slot_body_bb, next_frame_bb);

        // Slot body: load pointer from slot, load value, mark
        IRBuilder<> bsb(slot_body_bb);
        // GEP: struct field 2 is [0 x ptr], then index i into the array
        // Use i32 for struct indices, i64 for array index
        auto* slot_addr = bsb.CreateGEP(m_gc_root_frame_type, frame_phi,
            {ConstantInt::get(i32_ty, 0), ConstantInt::get(i32_ty, 2), i_phi});
        auto* obj_ptr = bsb.CreateLoad(i8_ptr, slot_addr, "obj_ptr");
        auto* obj_val = bsb.CreateLoad(obj_ty, obj_ptr, "obj_val");
        bsb.CreateCall(get_func("__ang_gc_mark_value"), {obj_val});
        auto* next_i = bsb.CreateAdd(i_phi, ConstantInt::get(i64_ty, 1));
        bsb.CreateBr(slot_loop_bb);
        i_phi->addIncoming(next_i, slot_body_bb);

        // Next frame: load prev_frame, loop back to frame_loop
        IRBuilder<> bnf(next_frame_bb);
        auto* prev_frame = bnf.CreateLoad(i8_ptr,
            bnf.CreateStructGEP(m_gc_root_frame_type, frame_phi, 0), "prev_frame");
        bnf.CreateBr(frame_loop_bb);
        frame_phi->addIncoming(prev_frame, next_frame_bb);

        // Next thread: load next_thread, loop back to thread_loop
        IRBuilder<> bnt(next_thread_bb);
        auto* next_thread = bnt.CreateLoad(i8_ptr,
            bnt.CreateStructGEP(m_gc_thread_state_type, thread_phi, 1), "next_thread");
        bnt.CreateBr(thread_loop_bb);
        thread_phi->addIncoming(next_thread, next_thread_bb);

        IRBuilder<> bd(done_bb);
        bd.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_collect() -> void
    // Mark roots + sweep. Single-threaded (Stage 5 adds mutex/STW).
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {}, false);
        auto* fn = createRuntimeFunc("__ang_gc_collect", fn_ty);
        m_fn_gc_collect = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);

        // Mark all roots
        b.CreateCall(get_func("__ang_gc_mark_roots"), {});

        // Sweep unreachable objects
        b.CreateCall(get_func("__ang_gc_sweep"), {});

        // Stats: increment collections
        auto* cols = b.CreateLoad(i64_ty, m_g_gc_collections, "cols");
        b.CreateStore(b.CreateAdd(cols, ConstantInt::get(i64_ty, 1)), m_g_gc_collections);

        // Reset allocation count so next collection triggers after threshold fresh allocations
        b.CreateStore(ConstantInt::get(i64_ty, 0), m_g_gc_count);

        b.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_push_frame(i8* frame_ptr) -> void
    // Push a root frame: frame->prev = thread->root_frames, thread->root_frames = frame
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {i8_ptr}, false);
        auto* fn = createRuntimeFunc("__ang_gc_push_frame", fn_ty);
        m_fn_gc_push_frame = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* has_tls_bb = BasicBlock::Create(m_ctx, "has_tls", fn);
        auto* done_bb = BasicBlock::Create(m_ctx, "done", fn);

        IRBuilder<> b(entry);
        auto* frame_ptr = fn->arg_begin();
        auto* tls = b.CreateLoad(i8_ptr, m_g_gc_thread_state_tls, "tls");
        auto* tls_ok = b.CreateICmpNE(tls, ConstantPointerNull::get(i8_ptr));
        b.CreateCondBr(tls_ok, has_tls_bb, done_bb);

        IRBuilder<> bht(has_tls_bb);
        // frame->prev_frame = thread->root_frames
        auto* root_frames = bht.CreateLoad(i8_ptr,
            bht.CreateStructGEP(m_gc_thread_state_type, tls, 2), "root_frames");
        bht.CreateStore(root_frames,
            bht.CreateStructGEP(m_gc_root_frame_type, frame_ptr, 0));
        // thread->root_frames = frame_ptr
        bht.CreateStore(frame_ptr,
            bht.CreateStructGEP(m_gc_thread_state_type, tls, 2));
        bht.CreateBr(done_bb);

        IRBuilder<> bd(done_bb);
        bd.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_pop_frame() -> void
    // Pop the top root frame: thread->root_frames = top->prev_frame
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {}, false);
        auto* fn = createRuntimeFunc("__ang_gc_pop_frame", fn_ty);
        m_fn_gc_pop_frame = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* has_tls_bb = BasicBlock::Create(m_ctx, "has_tls", fn);
        auto* has_frame_bb = BasicBlock::Create(m_ctx, "has_frame", fn);
        auto* done_bb = BasicBlock::Create(m_ctx, "done", fn);

        IRBuilder<> b(entry);
        auto* tls = b.CreateLoad(i8_ptr, m_g_gc_thread_state_tls, "tls");
        auto* tls_ok = b.CreateICmpNE(tls, ConstantPointerNull::get(i8_ptr));
        b.CreateCondBr(tls_ok, has_tls_bb, done_bb);

        IRBuilder<> bht(has_tls_bb);
        auto* top = bht.CreateLoad(i8_ptr,
            bht.CreateStructGEP(m_gc_thread_state_type, tls, 2), "top");
        auto* top_ok = bht.CreateICmpNE(top, ConstantPointerNull::get(i8_ptr));
        bht.CreateCondBr(top_ok, has_frame_bb, done_bb);

        IRBuilder<> bhf(has_frame_bb);
        auto* prev = bhf.CreateLoad(i8_ptr,
            bhf.CreateStructGEP(m_gc_root_frame_type, top, 0), "prev");
        bhf.CreateStore(prev,
            bhf.CreateStructGEP(m_gc_thread_state_type, tls, 2));
        bhf.CreateBr(done_bb);

        IRBuilder<> bd(done_bb);
        bd.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_thread_register(i8* state_ptr) -> void
    // Initialize state, store TLS, prepend to thread list.
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {i8_ptr}, false);
        auto* fn = createRuntimeFunc("__ang_gc_thread_register", fn_ty);
        m_fn_gc_thread_register = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* state = fn->arg_begin();

        // Init state fields
        b.CreateStore(state,
            b.CreateStructGEP(m_gc_thread_state_type, state, 0));    // self
        b.CreateStore(ConstantPointerNull::get(i8_ptr),
            b.CreateStructGEP(m_gc_thread_state_type, state, 1));    // next_thread = null
        b.CreateStore(ConstantPointerNull::get(i8_ptr),
            b.CreateStructGEP(m_gc_thread_state_type, state, 2));    // root_frames = null
        b.CreateStore(ConstantInt::getFalse(m_ctx),
            b.CreateStructGEP(m_gc_thread_state_type, state, 3));    // waiting = false

        // Store TLS = state
        b.CreateStore(state, m_g_gc_thread_state_tls);

        // Prepend to thread list: state->next_thread = @threads, @threads = state
        auto* old_head = b.CreateLoad(i8_ptr, m_g_gc_threads, "old_threads");
        b.CreateStore(old_head,
            b.CreateStructGEP(m_gc_thread_state_type, state, 1));
        b.CreateStore(state, m_g_gc_threads);

        b.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_thread_unregister(i8* state_ptr) -> void
    // Unlink from thread list, clear TLS.
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {i8_ptr}, false);
        auto* fn = createRuntimeFunc("__ang_gc_thread_unregister", fn_ty);
        m_fn_gc_thread_unregister = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* unlink_head_bb = BasicBlock::Create(m_ctx, "unlink_head", fn);
        auto* walk_bb = BasicBlock::Create(m_ctx, "walk", fn);
        auto* check_next_bb = BasicBlock::Create(m_ctx, "check_next", fn);
        auto* unlink_mid_bb = BasicBlock::Create(m_ctx, "unlink_mid", fn);
        auto* done_bb = BasicBlock::Create(m_ctx, "done", fn);

        IRBuilder<> b(entry);
        auto* state = fn->arg_begin();
        auto* head = b.CreateLoad(i8_ptr, m_g_gc_threads, "head");

        // Check if head is the one to remove
        auto* is_head = b.CreateICmpEQ(head, state);
        b.CreateCondBr(is_head, unlink_head_bb, walk_bb);

        // Unlink head: @threads = head->next_thread
        IRBuilder<> buh(unlink_head_bb);
        auto* next = buh.CreateLoad(i8_ptr,
            buh.CreateStructGEP(m_gc_thread_state_type, head, 1), "next");
        buh.CreateStore(next, m_g_gc_threads);
        buh.CreateBr(done_bb);

        // Walk: iterate linked list looking for state
        IRBuilder<> bw(walk_bb);
        auto* curr_phi = bw.CreatePHI(i8_ptr, 2, "curr");
        curr_phi->addIncoming(head, entry);
        auto* curr_null = bw.CreateICmpEQ(curr_phi, ConstantPointerNull::get(i8_ptr));
        bw.CreateCondBr(curr_null, done_bb, check_next_bb);

        // Check next: if curr->next == state, unlink
        IRBuilder<> bcn(check_next_bb);
        auto* curr_next = bcn.CreateLoad(i8_ptr,
            bcn.CreateStructGEP(m_gc_thread_state_type, curr_phi, 1), "curr_next");
        auto* found = bcn.CreateICmpEQ(curr_next, state);
        bcn.CreateCondBr(found, unlink_mid_bb, walk_bb);
        curr_phi->addIncoming(curr_next, check_next_bb);

        // Unlink middle: curr->next = state->next
        IRBuilder<> bum(unlink_mid_bb);
        auto* state_next = bum.CreateLoad(i8_ptr,
            bum.CreateStructGEP(m_gc_thread_state_type, state, 1), "state_next");
        bum.CreateStore(state_next,
            bum.CreateStructGEP(m_gc_thread_state_type, curr_phi, 1));
        bum.CreateBr(done_bb);

        // Done: clear TLS
        IRBuilder<> bd(done_bb);
        bd.CreateStore(ConstantPointerNull::get(i8_ptr), m_g_gc_thread_state_tls);
        bd.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_safepoint() -> void
    // If GC is running, set waiting=true, spin-wait, set waiting=false.
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {}, false);
        auto* fn = createRuntimeFunc("__ang_gc_safepoint", fn_ty);
        m_fn_gc_safepoint = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* set_wait_bb = BasicBlock::Create(m_ctx, "set_wait", fn);
        auto* do_set_bb = BasicBlock::Create(m_ctx, "do_set", fn);
        auto* spin_bb = BasicBlock::Create(m_ctx, "spin", fn);
        auto* resume_bb = BasicBlock::Create(m_ctx, "resume", fn);
        auto* do_clear_bb = BasicBlock::Create(m_ctx, "do_clear", fn);
        auto* done_bb = BasicBlock::Create(m_ctx, "done", fn);

        IRBuilder<> b(entry);
        auto* running = b.CreateLoad(i1_ty, m_g_gc_running, "running");
        b.CreateCondBr(running, set_wait_bb, done_bb);

        // Set waiting=true if TLS is available
        IRBuilder<> bsw(set_wait_bb);
        auto* tls = bsw.CreateLoad(i8_ptr, m_g_gc_thread_state_tls, "tls");
        auto* tls_ok = bsw.CreateICmpNE(tls, ConstantPointerNull::get(i8_ptr));
        bsw.CreateCondBr(tls_ok, do_set_bb, spin_bb);

        IRBuilder<> bds(do_set_bb);
        bds.CreateStore(ConstantInt::getTrue(m_ctx),
            bds.CreateStructGEP(m_gc_thread_state_type, tls, 3));
        bds.CreateBr(spin_bb);

        // Spin until gc_running is false
        IRBuilder<> bspin(spin_bb);
        auto* still_running = bspin.CreateLoad(i1_ty, m_g_gc_running, "still_running");
        bspin.CreateCondBr(still_running, spin_bb, resume_bb);

        // Resume: clear waiting flag
        IRBuilder<> br(resume_bb);
        auto* tls2 = br.CreateLoad(i8_ptr, m_g_gc_thread_state_tls, "tls2");
        auto* tls2_ok = br.CreateICmpNE(tls2, ConstantPointerNull::get(i8_ptr));
        br.CreateCondBr(tls2_ok, do_clear_bb, done_bb);

        IRBuilder<> bdc(do_clear_bb);
        bdc.CreateStore(ConstantInt::getFalse(m_ctx),
            bdc.CreateStructGEP(m_gc_thread_state_type, tls2, 3));
        bdc.CreateBr(done_bb);

        IRBuilder<> bd(done_bb);
        bd.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_pin(AngaraObject val) -> void
    // Set the pinned bit (bit 16 of meta) so the object survives collection.
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_gc_pin", fn_ty);
        m_fn_gc_pin = FunctionCallee(fn);

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
        auto* meta_gaddr = b2.CreateStructGEP(header_ty, obj_ptr, 1);
        auto* meta = b2.CreateLoad(i32_ty, meta_gaddr, "meta");
        auto* pinned_meta = b2.CreateOr(meta, ConstantInt::get(i32_ty, 1 << 16), "pinned_meta");
        b2.CreateStore(pinned_meta, meta_gaddr);
        b2.CreateBr(done_bb);

        IRBuilder<> bd(done_bb);
        bd.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_unpin(AngaraObject val) -> void
    // Clear the pinned bit (bit 16 of meta).
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_gc_unpin", fn_ty);
        m_fn_gc_unpin = FunctionCallee(fn);

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
        auto* meta_gaddr = b2.CreateStructGEP(header_ty, obj_ptr, 1);
        auto* meta = b2.CreateLoad(i32_ty, meta_gaddr, "meta");
        auto* unpinned_meta = b2.CreateAnd(meta, ConstantInt::get(i32_ty, ~(1 << 16)), "unpinned_meta");
        b2.CreateStore(unpinned_meta, meta_gaddr);
        b2.CreateBr(done_bb);

        IRBuilder<> bd(done_bb);
        bd.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_print_stats() -> void
    // Print GC statistics via individual printf calls to avoid variadic
    // ABI issues on arm64 (2+ variadic i64 args cause crashes).
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {}, false);
        auto* fn = createRuntimeFunc("__ang_gc_print_stats", fn_ty);
        m_fn_gc_print_stats = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);

        // Load stats
        auto* cols = b.CreateLoad(i64_ty, m_g_gc_collections, "cols");
        auto* allocs = b.CreateLoad(i64_ty, m_g_gc_total_allocs, "allocs");
        auto* frees = b.CreateLoad(i64_ty, m_g_gc_total_frees, "frees");
        auto* live = b.CreateSub(allocs, frees, "live");

        auto* printf_ty = FunctionType::get(i32_ty, {PointerType::get(m_ctx, 0)}, true);
        auto* printf_fn = cast<Function>(
            m_module.getOrInsertFunction("printf", printf_ty).getCallee());

        // Print each stat individually (1 variadic arg per call)
        auto* f1 = b.CreateGlobalStringPtr("[GC] collections: %lld | ", "f1");
        b.CreateCall(printf_fn, {f1, cols});

        auto* f2 = b.CreateGlobalStringPtr("allocs: %lld | ", "f2");
        b.CreateCall(printf_fn, {f2, allocs});

        auto* f3 = b.CreateGlobalStringPtr("freed: %lld | ", "f3");
        b.CreateCall(printf_fn, {f3, frees});

        auto* f4 = b.CreateGlobalStringPtr("live: %lld\n", "f4");
        b.CreateCall(printf_fn, {f4, live});

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
    stub_void("__ang_gc_print_stats", FunctionType::get(void_ty, {}, false));
}

} // namespace angara
