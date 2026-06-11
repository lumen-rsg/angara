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

        // Safepoint check: if GC is running, spin-wait
        auto* running = b.CreateLoad(i1_ty, m_g_gc_running, "running");
        b.CreateCondBr(running, safepoint_bb, alloc_bb);

        IRBuilder<> bsp(safepoint_bb);
        auto* still_running = bsp.CreateLoad(i1_ty, m_g_gc_running, "still_running");
        bsp.CreateCondBr(still_running, safepoint_bb, alloc_bb);

        // Allocate and initialize
        IRBuilder<> ba(alloc_bb);
        auto* mem = ba.CreateCall(malloc_fn, {size_arg}, "mem");

        // Initialize header: type
        auto* type_addr = ba.CreateStructGEP(header_ty, mem, 0);
        ba.CreateStore(type_arg, type_addr);

        // Initialize header: meta = packMeta(WHITE, true)
        auto* meta_addr = ba.CreateStructGEP(header_ty, mem, 1);
        ba.CreateStore(ConstantInt::get(i32_ty, packMeta(COLOR_WHITE, true)), meta_addr);

        // Initialize header: next = current head, prepend to list
        auto* next_addr = ba.CreateStructGEP(header_ty, mem, 2);
        auto* old_head = ba.CreateLoad(i8_ptr, m_g_gc_head, "old_head");
        ba.CreateStore(old_head, next_addr);
        ba.CreateStore(mem, m_g_gc_head);

        // Increment count
        auto* count = ba.CreateLoad(i64_ty, m_g_gc_count, "count");
        auto* new_count = ba.CreateAdd(count, ConstantInt::get(i64_ty, 1), "new_count");
        ba.CreateStore(new_count, m_g_gc_count);

        // Check threshold
        auto* threshold = ba.CreateLoad(i64_ty, m_g_gc_threshold, "threshold");
        auto* over = ba.CreateICmpSGE(new_count, threshold, "over_threshold");
        ba.CreateCondBr(over, collect_bb, done_bb);

        // Collect if over threshold
        IRBuilder<> bc(collect_bb);
        bc.CreateCall(get_func("__ang_gc_collect"), {});
        bc.CreateBr(done_bb);

        IRBuilder<> bd(done_bb);
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
    // For OBJ_NATIVE_INSTANCE: call the finalize function pointer.
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {i8_ptr}, false);
        auto* fn = createRuntimeFunc("__ang_gc_finalize", fn_ty);
        m_fn_gc_finalize = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* is_native_bb = BasicBlock::Create(m_ctx, "is_native", fn);
        auto* call_finalize_bb = BasicBlock::Create(m_ctx, "call_finalize", fn);
        auto* done_bb = BasicBlock::Create(m_ctx, "done", fn);

        IRBuilder<> b(entry);
        auto* obj_ptr = fn->arg_begin();
        auto* type_gaddr = b.CreateStructGEP(header_ty, obj_ptr, 0);
        auto* obj_type = b.CreateLoad(i32_ty, type_gaddr, "obj_type");
        auto* is_native = b.CreateICmpEQ(obj_type, ConstantInt::get(i32_ty, OBJ_NATIVE_INSTANCE));
        b.CreateCondBr(is_native, is_native_bb, done_bb);

        IRBuilder<> bn(is_native_bb);
        // AngaraNativeInstance: { ObjHeader, i8* data, i8* finalize_fn, i8* type_name }
        auto* finalize_fn = bn.CreateLoad(i8_ptr,
            bn.CreateStructGEP(m_native_instance_type, obj_ptr, 2), "finalize_fn");
        auto* has_fn = bn.CreateICmpNE(finalize_fn, ConstantPointerNull::get(i8_ptr));
        bn.CreateCondBr(has_fn, call_finalize_bb, done_bb);

        IRBuilder<> bcf(call_finalize_bb);
        auto* data = bcf.CreateLoad(i8_ptr,
            bcf.CreateStructGEP(m_native_instance_type, obj_ptr, 1), "data");
        // Call finalize_fn(data) — it's a void(*)(void*)
        auto* finalize_ty = FunctionType::get(void_ty, {i8_ptr}, false);
        bcf.CreateCall(finalize_ty, finalize_fn, {data});
        bcf.CreateBr(done_bb);

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

        // WHITE: unlink and free
        IRBuilder<> bw(is_white_bb);
        // Get next pointer before we free
        auto* next_ptr = bw.CreateLoad(i8_ptr,
            bw.CreateStructGEP(header_ty, curr_phi, 2), "next");
        // Check if prev is null (head of list)
        auto* prev_is_null = bw.CreateICmpEQ(prev_phi, ConstantPointerNull::get(i8_ptr));
        bw.CreateCondBr(prev_is_null, unlink_head_bb, unlink_mid_bb);

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
        bf.CreateBr(advance_bb);

        // BLACK: reset to WHITE + unique for next cycle, advance prev
        IRBuilder<> bbk(is_black_bb);
        auto* next_ptr2 = bbk.CreateLoad(i8_ptr,
            bbk.CreateStructGEP(header_ty, curr_phi, 2), "next2");
        auto* reset_meta = ConstantInt::get(i32_ty, packMeta(COLOR_WHITE, true));
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

        b.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_push_frame(i8* frame_ptr) -> void  [Stage 5 stub]
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {i8_ptr}, false);
        auto* fn = createRuntimeFunc("__ang_gc_push_frame", fn_ty);
        m_fn_gc_push_frame = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        b.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_pop_frame() -> void  [Stage 5 stub]
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {}, false);
        auto* fn = createRuntimeFunc("__ang_gc_pop_frame", fn_ty);
        m_fn_gc_pop_frame = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        b.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_thread_register(i8* state_ptr) -> void  [Stage 5 stub]
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {i8_ptr}, false);
        auto* fn = createRuntimeFunc("__ang_gc_thread_register", fn_ty);
        m_fn_gc_thread_register = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        b.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_thread_unregister(i8* state_ptr) -> void  [Stage 5 stub]
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {i8_ptr}, false);
        auto* fn = createRuntimeFunc("__ang_gc_thread_unregister", fn_ty);
        m_fn_gc_thread_unregister = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        b.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_safepoint() -> void  [Stage 5 stub]
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
    // __ang_gc_pin(AngaraObject val) -> void  [Stage 6 stub]
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_gc_pin", fn_ty);
        m_fn_gc_pin = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        b.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_unpin(AngaraObject val) -> void  [Stage 6 stub]
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_gc_unpin", fn_ty);
        m_fn_gc_unpin = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
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
