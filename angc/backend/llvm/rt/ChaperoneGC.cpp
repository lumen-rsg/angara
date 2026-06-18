#include "ChaperoneGC.h"
#include "RuntimeBuilder.h"

using namespace llvm;

namespace angara {

ChaperoneGC::ChaperoneGC(LLVMContext& ctx, Module& module, IRBuilder<>& builder)
    : m_ctx(ctx), m_module(module), m_builder(builder) {}

// ---------------------------------------------------------------------------
// GC swap extensibility
// ---------------------------------------------------------------------------

llvm::ConstantInt* ChaperoneGC::getInitialMetaConstant() const {
    return ConstantInt::get(Type::getInt32Ty(m_ctx), packMeta(COLOR_WHITE, true));
}

// ---------------------------------------------------------------------------
// Type generation
// ---------------------------------------------------------------------------

void ChaperoneGC::generateTypes() {
    auto& ctx = m_ctx;

    // ObjHeader: { i32 type, i32 meta, i8* forward }
    // Same field count and GEP indices as MarkSweepGC for binary compatibility.
    // meta packs: byte 0 = color, bit 8 = is_unique, bit 16 = pinned, byte 3 = arena_id
    // forward: null during normal operation; points to new copy during relocation
    m_obj_header_type = StructType::create(ctx, {
        Type::getInt32Ty(ctx),     // field 0: type   (OBJ_STRING, OBJ_LIST, ...)
        Type::getInt32Ty(ctx),     // field 1: meta   (color | (is_unique << 8) | (pinned << 16) | (arena_id << 24))
        PointerType::get(ctx, 0)   // field 2: forward (forwarding pointer / allocation link)
    }, "ObjHeader");

    // ArenaHeader: { i8* base, i8* bump, i8* limit, i64 object_count, i32 arena_id, i8* next_arena, i8* free_list }
    m_arena_header_type = StructType::create(ctx, {
        PointerType::get(ctx, 0),  // base   (start of usable memory after this header)
        PointerType::get(ctx, 0),  // bump   (current bump pointer)
        PointerType::get(ctx, 0),  // limit  (end of the arena)
        Type::getInt64Ty(ctx),     // object_count (live objects in this arena)
        Type::getInt32Ty(ctx),     // arena_id
        PointerType::get(ctx, 0),  // next_arena (linked list for this thread's arenas)
        PointerType::get(ctx, 0)   // free_list (linked list of recycled object slots)
    }, "ArenaHeader");

    // GcThreadState: per-thread data for the collector
    // { i8* self, i8* next_thread, i8* root_frames, i1 waiting, i8* current_arena }
    m_gc_thread_state_type = StructType::create(ctx, {
        PointerType::get(ctx, 0),  // self
        PointerType::get(ctx, 0),  // next_thread
        PointerType::get(ctx, 0),  // root_frames
        Type::getInt1Ty(ctx),      // waiting
        PointerType::get(ctx, 0)   // current_arena
    }, "GcThreadState");

    // GcRootFrame: per-function root frame pushed at entry, popped at exit
    // { i8* prev_frame, i32 count, [0 x AngaraObject*] }
    m_gc_root_frame_type = StructType::create(ctx, {
        PointerType::get(ctx, 0),  // prev_frame
        Type::getInt32Ty(ctx),     // count
        ArrayType::get(PointerType::get(ctx, 0), 0)  // slots
    }, "GcRootFrame");
}

// ---------------------------------------------------------------------------
// Global variable generation
// ---------------------------------------------------------------------------

void ChaperoneGC::generateGlobals() {
    auto& ctx = m_ctx;

    // Arena pool: array of arena pointers
    m_g_gc_arenas = new GlobalVariable(
        m_module, ArrayType::get(PointerType::get(ctx, 0), 256),
        false, GlobalValue::InternalLinkage,
        ConstantAggregateZero::get(ArrayType::get(PointerType::get(ctx, 0), 256)),
        "__ang_gc_arenas");

    // Allocation linked-list head (all live objects chained via forward field)
    m_g_gc_alloc_list = new GlobalVariable(
        m_module, PointerType::get(ctx, 0),
        false, GlobalValue::InternalLinkage,
        ConstantPointerNull::get(PointerType::get(ctx, 0)),
        "__ang_gc_alloc_list");

    m_g_gc_arena_count = new GlobalVariable(
        m_module, Type::getInt64Ty(ctx),
        false, GlobalValue::InternalLinkage,
        ConstantInt::get(Type::getInt64Ty(ctx), 0),
        "__ang_gc_arena_count");

    // Free arena list (recycled arenas)
    m_g_gc_arena_free = new GlobalVariable(
        m_module, PointerType::get(ctx, 0),
        false, GlobalValue::InternalLinkage,
        ConstantPointerNull::get(PointerType::get(ctx, 0)),
        "__ang_gc_arena_free");

    // Arena pool spinlock
    m_g_gc_arena_lock = new GlobalVariable(
        m_module, Type::getInt32Ty(ctx),
        false, GlobalValue::InternalLinkage,
        ConstantInt::get(Type::getInt32Ty(ctx), 0),
        "__ang_gc_arena_lock");

    // Total live objects across all arenas
    m_g_gc_total_count = new GlobalVariable(
        m_module, Type::getInt64Ty(ctx),
        false, GlobalValue::InternalLinkage,
        ConstantInt::get(Type::getInt64Ty(ctx), 0),
        "__ang_gc_total_count");

    // Collection threshold
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

    // Thread list
    m_g_gc_threads = new GlobalVariable(
        m_module, PointerType::get(ctx, 0),
        false, GlobalValue::InternalLinkage,
        ConstantPointerNull::get(PointerType::get(ctx, 0)),
        "__ang_gc_threads");

    // Thread-local state
    m_g_gc_thread_state_tls = new GlobalVariable(
        m_module, PointerType::get(ctx, 0),
        false, GlobalValue::InternalLinkage,
        ConstantPointerNull::get(PointerType::get(ctx, 0)),
        "__ang_gc_thread_state");
    m_g_gc_thread_state_tls->setThreadLocal(true);

    // Stats
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

    // Chaperone active flag
    m_g_gc_chaperone_active = new GlobalVariable(
        m_module, Type::getInt1Ty(ctx),
        false, GlobalValue::InternalLinkage,
        ConstantInt::getFalse(ctx),
        "__ang_gc_chaperone_active");

    // Simulated annealing temperature (starts at 1.0)
    m_g_gc_temperature = new GlobalVariable(
        m_module, Type::getDoubleTy(ctx),
        false, GlobalValue::InternalLinkage,
        ConstantFP::get(Type::getDoubleTy(ctx), 1.0),
        "__ang_gc_temperature");

    // Step counter for annealing schedule
    m_g_gc_step_count = new GlobalVariable(
        m_module, Type::getInt64Ty(ctx),
        false, GlobalValue::InternalLinkage,
        ConstantInt::get(Type::getInt64Ty(ctx), 0),
        "__ang_gc_step_count");

    // Chaperone thread handle (pthread_t stored as i8*)
    m_g_gc_chaperone_thread = new GlobalVariable(
        m_module, PointerType::get(ctx, 0),
        false, GlobalValue::InternalLinkage,
        ConstantPointerNull::get(PointerType::get(ctx, 0)),
        "__ang_gc_chaperone_thread");

    // LCG random state
    m_g_gc_rand_state = new GlobalVariable(
        m_module, Type::getInt64Ty(ctx),
        false, GlobalValue::InternalLinkage,
        ConstantInt::get(Type::getInt64Ty(ctx), 12345),
        "__ang_gc_rand_state");
}

// ---------------------------------------------------------------------------
// Function generation
// ---------------------------------------------------------------------------

llvm::Function* ChaperoneGC::createRuntimeFunc(const std::string& name, llvm::FunctionType* type) {
    auto callee = m_module.getOrInsertFunction(name, type);
    auto* fn = cast<Function>(callee.getCallee());
    fn->setLinkage(Function::InternalLinkage);
    fn->setDSOLocal(true);
    fn->addFnAttr(llvm::Attribute::NoInline);
    return fn;
}

void ChaperoneGC::generateFunctions() {
    auto* void_ty = Type::getVoidTy(m_ctx);
    auto* i1_ty   = Type::getInt1Ty(m_ctx);
    auto* i8_ty   = Type::getInt8Ty(m_ctx);
    auto* i32_ty  = Type::getInt32Ty(m_ctx);
    auto* i64_ty  = Type::getInt64Ty(m_ctx);
    auto* i8_ptr  = PointerType::get(m_ctx, 0);
    auto* obj_ty  = m_angara_obj_type;
    auto* header_ty = m_obj_header_type;
    auto* arena_ty  = m_arena_header_type;

    auto* malloc_fn = m_module.getFunction("malloc");
    auto* free_fn   = m_module.getFunction("free");

    // Declare additional C library functions needed by the chaperone
    auto* f64_ty = Type::getDoubleTy(m_ctx);
    m_module.getOrInsertFunction("exp", FunctionType::get(f64_ty, {f64_ty}, false));
    m_module.getOrInsertFunction("usleep", FunctionType::get(i32_ty, {i32_ty}, false));
    m_module.getOrInsertFunction("rand", FunctionType::get(i32_ty, {}, false));
    m_module.getOrInsertFunction("srand", FunctionType::get(void_ty, {i32_ty}, false));

    auto get_func = [&](const std::string& name) -> Function* {
        return m_module.getFunction(name);
    };

    // ===================================================================
    // Pre-declare mutually recursive functions
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
    {
        auto* ty = FunctionType::get(i8_ptr, {i64_ty, i32_ty}, false);
        auto* fn = cast<Function>(m_module.getOrInsertFunction("__ang_gc_alloc_slow", ty).getCallee());
        fn->setLinkage(Function::InternalLinkage); fn->setDSOLocal(true);
    }
    {
        auto* ty = FunctionType::get(i8_ptr, {i8_ptr}, false);
        auto* fn = cast<Function>(m_module.getOrInsertFunction("__ang_gc_read_barrier", ty).getCallee());
        fn->setLinkage(Function::InternalLinkage); fn->setDSOLocal(true);
    }
    // Object size helper (returns 16-aligned allocation size from ObjHeader.type)
    {
        auto* ty = FunctionType::get(i64_ty, {i8_ptr}, false);
        auto* fn = cast<Function>(m_module.getOrInsertFunction("__ang_gc_obj_size", ty).getCallee());
        fn->setLinkage(Function::InternalLinkage); fn->setDSOLocal(true);
    }
    // Update-ref helpers for chaperone relocation
    {
        auto* ty = FunctionType::get(void_ty, {i8_ptr, i64_ty, i64_ty}, false);
        auto* fn = cast<Function>(m_module.getOrInsertFunction("__ang_gc_update_ref_in_value", ty).getCallee());
        fn->setLinkage(Function::InternalLinkage); fn->setDSOLocal(true);
    }
    {
        auto* ty = FunctionType::get(void_ty, {i64_ty, i64_ty}, false);
        auto* fn = cast<Function>(m_module.getOrInsertFunction("__ang_gc_update_refs_heap", ty).getCallee());
        fn->setLinkage(Function::InternalLinkage); fn->setDSOLocal(true);
    }
    {
        auto* ty = FunctionType::get(void_ty, {}, false);
        auto* fn = cast<Function>(m_module.getOrInsertFunction("__ang_gc_recycle_arenas", ty).getCallee());
        fn->setLinkage(Function::InternalLinkage); fn->setDSOLocal(true);
    }
    // Chaperone functions
    {
        auto* ty = FunctionType::get(i8_ptr, {i8_ptr}, false);
        auto* fn = cast<Function>(m_module.getOrInsertFunction("__ang_gc_chaperone_main", ty).getCallee());
        fn->setLinkage(Function::InternalLinkage); fn->setDSOLocal(true);
    }
    {
        auto* ty = FunctionType::get(void_ty, {}, false);
        auto* fn = cast<Function>(m_module.getOrInsertFunction("__ang_gc_chaperone_step", ty).getCallee());
        fn->setLinkage(Function::InternalLinkage); fn->setDSOLocal(true);
    }
    {
        auto* ty = FunctionType::get(f64_ty, {i8_ptr}, false);
        auto* fn = cast<Function>(m_module.getOrInsertFunction("__ang_gc_compute_energy", ty).getCallee());
        fn->setLinkage(Function::InternalLinkage); fn->setDSOLocal(true);
    }
    {
        auto* ty = FunctionType::get(i8_ptr, {i8_ptr, i8_ptr}, false);
        auto* fn = cast<Function>(m_module.getOrInsertFunction("__ang_gc_relocate", ty).getCallee());
        fn->setLinkage(Function::InternalLinkage); fn->setDSOLocal(true);
    }
    {
        auto* ty = FunctionType::get(void_ty, {i8_ptr, i8_ptr}, false);
        auto* fn = cast<Function>(m_module.getOrInsertFunction("__ang_gc_update_refs", ty).getCallee());
        fn->setLinkage(Function::InternalLinkage); fn->setDSOLocal(true);
    }
    {
        auto* ty = FunctionType::get(i8_ptr, {}, false);
        auto* fn = cast<Function>(m_module.getOrInsertFunction("__ang_gc_chaperone_spawn", ty).getCallee());
        fn->setLinkage(Function::InternalLinkage); fn->setDSOLocal(true);
    }
    {
        auto* fn_ty = FunctionType::get(i8_ptr, {i64_ty, i32_ty}, false);
        auto* fn = createRuntimeFunc("__ang_gc_alloc", fn_ty);
        m_fn_gc_alloc = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* safepoint_bb = BasicBlock::Create(m_ctx, "safepoint", fn);
        auto* spin_bb = BasicBlock::Create(m_ctx, "spin", fn);
        auto* clear_wait_bb = BasicBlock::Create(m_ctx, "clear_wait", fn);
        auto* try_bump_bb = BasicBlock::Create(m_ctx, "try_bump", fn);
        auto* bump_ok_bb = BasicBlock::Create(m_ctx, "bump_ok", fn);
        auto* slow_bb = BasicBlock::Create(m_ctx, "slow", fn);

        IRBuilder<> b(entry);
        auto* size_arg = fn->arg_begin();
        auto* type_arg = fn->arg_begin() + 1;

        // Safepoint: if GC running, spin-wait
        auto* running = b.CreateLoad(i1_ty, m_g_gc_running, "running");
        b.CreateCondBr(running, safepoint_bb, try_bump_bb);

        // Safepoint spin-wait (same pattern as MarkSweepGC)
        IRBuilder<> bsp(safepoint_bb);
        auto* tls_ptr = bsp.CreateLoad(i8_ptr, m_g_gc_thread_state_tls, "tls");
        auto* tls_ok = bsp.CreateICmpNE(tls_ptr, ConstantPointerNull::get(i8_ptr));
        auto* set_wait_bb = BasicBlock::Create(m_ctx, "set_wait", fn);
        bsp.CreateCondBr(tls_ok, set_wait_bb, spin_bb);

        IRBuilder<> bsw(set_wait_bb);
        bsw.CreateStore(ConstantInt::getTrue(m_ctx),
            bsw.CreateStructGEP(m_gc_thread_state_type, tls_ptr, 3));
        bsw.CreateBr(spin_bb);

        IRBuilder<> bspin(spin_bb);
        auto* still_running = bspin.CreateLoad(i1_ty, m_g_gc_running, "still_running");
        bspin.CreateCondBr(still_running, spin_bb, clear_wait_bb);

        IRBuilder<> bcw(clear_wait_bb);
        auto* tls2 = bcw.CreateLoad(i8_ptr, m_g_gc_thread_state_tls, "tls2");
        auto* tls2_ok = bcw.CreateICmpNE(tls2, ConstantPointerNull::get(i8_ptr));
        auto* do_clear_bb = BasicBlock::Create(m_ctx, "do_clear", fn);
        bcw.CreateCondBr(tls2_ok, do_clear_bb, try_bump_bb);

        IRBuilder<> bdc(do_clear_bb);
        bdc.CreateStore(ConstantInt::getFalse(m_ctx),
            bdc.CreateStructGEP(m_gc_thread_state_type, tls2, 3));
        bdc.CreateBr(try_bump_bb);

        // Try bump allocation from thread-local arena
        IRBuilder<> bt(try_bump_bb);
        auto* tls3 = bt.CreateLoad(i8_ptr, m_g_gc_thread_state_tls, "tls3");
        auto* has_tls = bt.CreateICmpNE(tls3, ConstantPointerNull::get(i8_ptr));
        auto* load_arena_bb = BasicBlock::Create(m_ctx, "load_arena", fn);
        bt.CreateCondBr(has_tls, load_arena_bb, slow_bb);

        IRBuilder<> bla(load_arena_bb);
        auto* arena = bla.CreateLoad(i8_ptr,
            bla.CreateStructGEP(m_gc_thread_state_type, tls3, 4), "arena");

        // If no arena yet, go slow
        auto* has_arena = bla.CreateICmpNE(arena, ConstantPointerNull::get(i8_ptr));
        auto* try_free_bb = BasicBlock::Create(m_ctx, "try_free", fn);
        bla.CreateCondBr(has_arena, try_free_bb, slow_bb);

        // Try to reuse a freed slot from the arena's free list
        IRBuilder<> btf(try_free_bb);
        auto* free_list = btf.CreateLoad(i8_ptr,
            btf.CreateStructGEP(m_arena_header_type, arena, 6), "free_list");
        auto* has_free = btf.CreateICmpNE(free_list, ConstantPointerNull::get(i8_ptr));
        auto* use_free_bb = BasicBlock::Create(m_ctx, "use_free", fn);
        btf.CreateCondBr(has_free, use_free_bb, bump_ok_bb);

        // Pop from free list: slot = free_list; free_list = slot->forward
        IRBuilder<> buf(use_free_bb);
        auto* next_free = buf.CreateLoad(i8_ptr,
            buf.CreateStructGEP(header_ty, free_list, 2), "next_free");
        buf.CreateStore(next_free, buf.CreateStructGEP(m_arena_header_type, arena, 6));
        // Initialize header for reused slot (keep arena_id from previous occupant)
        buf.CreateStore(type_arg, buf.CreateStructGEP(header_ty, free_list, 0));
        auto* old_meta = buf.CreateLoad(i32_ty,
            buf.CreateStructGEP(header_ty, free_list, 1), "old_meta");
        auto* old_arena_id = buf.CreateAnd(old_meta, ConstantInt::get(i32_ty, 0xFF000000));
        auto* new_meta = buf.CreateOr(ConstantInt::get(i32_ty, packMeta(COLOR_WHITE, true)),
            old_arena_id);
        buf.CreateStore(new_meta, buf.CreateStructGEP(header_ty, free_list, 1));
        buf.CreateStore(ConstantPointerNull::get(i8_ptr),
            buf.CreateStructGEP(header_ty, free_list, 2));
        auto* ret_free_bb = BasicBlock::Create(m_ctx, "ret_free", fn);
        buf.CreateBr(ret_free_bb);

        IRBuilder<> brf(ret_free_bb);
        brf.CreateRet(free_list);

        // Bump allocation: load bump, advance, check limit
        IRBuilder<> bb(bump_ok_bb);
        auto* bump = bb.CreateLoad(i8_ptr,
            bb.CreateStructGEP(m_arena_header_type, arena, 1), "bump");
        auto* limit = bb.CreateLoad(i8_ptr,
            bb.CreateStructGEP(m_arena_header_type, arena, 2), "limit");

        // Align size up to 16 bytes
        auto* aligned_size = bb.CreateAnd(
            bb.CreateAdd(size_arg, ConstantInt::get(i64_ty, 15)),
            ConstantInt::get(i64_ty, ~15), "aligned_size");

        auto* new_bump = bb.CreateGEP(i8_ty, bump, aligned_size, "new_bump");
        auto* fits = bb.CreateICmpULE(new_bump, limit, "fits");
        auto* do_bump_bb = BasicBlock::Create(m_ctx, "do_bump", fn);
        bb.CreateCondBr(fits, do_bump_bb, slow_bb);

        // Fast path: bump succeeded
        IRBuilder<> bdb(do_bump_bb);
        // Store new bump pointer
        bdb.CreateStore(new_bump,
            bdb.CreateStructGEP(m_arena_header_type, arena, 1));
        // Increment object_count
        auto* count_addr = bdb.CreateStructGEP(m_arena_header_type, arena, 3);
        auto* old_count = bdb.CreateLoad(i64_ty, count_addr, "old_count");
        bdb.CreateStore(bdb.CreateAdd(old_count, ConstantInt::get(i64_ty, 1)), count_addr);

        // Initialize header at bump
        auto* type_addr = bdb.CreateStructGEP(header_ty, bump, 0);
        bdb.CreateStore(type_arg, type_addr);
        auto* meta_addr = bdb.CreateStructGEP(header_ty, bump, 1);
        // Include arena_id in meta for free-list recycling during sweep
        auto* arena_id = bdb.CreateLoad(i32_ty,
            bdb.CreateStructGEP(m_arena_header_type, arena, 4), "arena_id");
        auto* arena_id_shifted = bdb.CreateShl(
            bdb.CreateAnd(arena_id, ConstantInt::get(i32_ty, 0xFF)),
            ConstantInt::get(i32_ty, 24), "arena_id_shifted");
        auto* base_meta = ConstantInt::get(i32_ty, packMeta(COLOR_WHITE, true));
        bdb.CreateStore(bdb.CreateOr(base_meta, arena_id_shifted), meta_addr);
        auto* fwd_addr = bdb.CreateStructGEP(header_ty, bump, 2);
        bdb.CreateStore(ConstantPointerNull::get(i8_ptr), fwd_addr);

        // Stats (only total_count needed for threshold check)
        auto* total_count2 = bdb.CreateLoad(i64_ty, m_g_gc_total_count, "total_count");
        bdb.CreateStore(bdb.CreateAdd(total_count2, ConstantInt::get(i64_ty, 1)), m_g_gc_total_count);

        // Check threshold
        auto* new_total = bdb.CreateAdd(total_count2, ConstantInt::get(i64_ty, 1));
        auto* threshold = bdb.CreateLoad(i64_ty, m_g_gc_threshold, "threshold");
        auto* over = bdb.CreateICmpSGE(new_total, threshold, "over_threshold");
        auto* collect_bb = BasicBlock::Create(m_ctx, "do_collect", fn);
        auto* ret_bb = BasicBlock::Create(m_ctx, "ret", fn);
        bdb.CreateCondBr(over, collect_bb, ret_bb);

        // Collect if over threshold
        IRBuilder<> bc(collect_bb);
        bc.CreateCall(get_func("__ang_gc_collect"), {});
        bc.CreateBr(ret_bb);

        // Return bump pointer
        IRBuilder<> br(ret_bb);
        br.CreateRet(bump);

        // Slow path: arena exhaustion or no arena
        IRBuilder<> bs(slow_bb);
        auto* mem = bs.CreateCall(get_func("__ang_gc_alloc_slow"), {size_arg, type_arg}, "mem");
        bs.CreateRet(mem);
    }

    // ===================================================================
    // __ang_gc_alloc_slow(i64 size, i32 obj_type) -> i8*
    // Called when bump allocation fails. Allocates a new arena or
    // triggers GC and retries.
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(i8_ptr, {i64_ty, i32_ty}, false);
        auto* fn = createRuntimeFunc("__ang_gc_alloc_slow", fn_ty);
        m_fn_gc_alloc_slow = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* use_free_bb = BasicBlock::Create(m_ctx, "use_free", fn);
        auto* alloc_new_bb = BasicBlock::Create(m_ctx, "alloc_new", fn);
        auto* init_arena_bb = BasicBlock::Create(m_ctx, "init_arena", fn);
        auto* retry_bb = BasicBlock::Create(m_ctx, "retry", fn);

        IRBuilder<> b(entry);
        auto* size_arg = fn->arg_begin();
        auto* type_arg = fn->arg_begin() + 1;

        // Load TLS
        auto* tls = b.CreateLoad(i8_ptr, m_g_gc_thread_state_tls, "tls");
        auto* has_tls = b.CreateICmpNE(tls, ConstantPointerNull::get(i8_ptr));

        // Fallback: if no TLS (shouldn't happen, but safety), raw malloc
        auto* no_tls_bb = BasicBlock::Create(m_ctx, "no_tls", fn);
        auto* has_tls_bb = BasicBlock::Create(m_ctx, "has_tls_bb", fn);
        b.CreateCondBr(has_tls, has_tls_bb, no_tls_bb);

        // No TLS fallback: raw malloc
        {
            IRBuilder<> bnt(no_tls_bb);
            auto* mem = bnt.CreateCall(malloc_fn, {size_arg}, "raw_mem");
            auto* type_addr = bnt.CreateStructGEP(header_ty, mem, 0);
            bnt.CreateStore(type_arg, type_addr);
            auto* meta_addr = bnt.CreateStructGEP(header_ty, mem, 1);
            bnt.CreateStore(ConstantInt::get(i32_ty, packMeta(COLOR_WHITE, true)), meta_addr);
            auto* fwd_addr = bnt.CreateStructGEP(header_ty, mem, 2);
            bnt.CreateStore(ConstantPointerNull::get(i8_ptr), fwd_addr);
            bnt.CreateRet(mem);
        }

        IRBuilder<> bht(has_tls_bb);
        // Try to get a free arena from the pool
        auto* free_arena = bht.CreateLoad(i8_ptr, m_g_gc_arena_free, "free_arena");
        auto* has_free = bht.CreateICmpNE(free_arena, ConstantPointerNull::get(i8_ptr));
        bht.CreateCondBr(has_free, use_free_bb, alloc_new_bb);

        // Use a recycled arena
        IRBuilder<> buf(use_free_bb);
        // Pop from free list: arena_free = free_arena->next_arena
        auto* next_free = buf.CreateLoad(i8_ptr,
            buf.CreateStructGEP(m_arena_header_type, free_arena, 5), "next_free");
        buf.CreateStore(next_free, m_g_gc_arena_free);
        // Reset bump pointer to base
        auto* base = buf.CreateLoad(i8_ptr,
            buf.CreateStructGEP(m_arena_header_type, free_arena, 0), "base");
        buf.CreateStore(base, buf.CreateStructGEP(m_arena_header_type, free_arena, 1));
        // Set as thread's current arena
        buf.CreateStore(free_arena, buf.CreateStructGEP(m_gc_thread_state_type, tls, 4));
        buf.CreateBr(retry_bb);

        // Allocate a new arena via malloc
        IRBuilder<> ban(alloc_new_bb);
        auto* arena_alloc_size = ConstantInt::get(i64_ty, ARENA_SIZE);
        auto* arena_mem = ban.CreateCall(malloc_fn, {arena_alloc_size}, "arena_mem");
        ban.CreateBr(init_arena_bb);

        // Init arena header
        IRBuilder<> bi(init_arena_bb);
        // base = arena_mem + sizeof(ArenaHeader) [skip the header]
        // We lay out ArenaHeader at the start, objects after it
        auto* header_size_val = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(m_arena_header_type));
        auto* base_ptr = bi.CreateGEP(i8_ty, arena_mem, {header_size_val}, "base");
        bi.CreateStore(base_ptr, bi.CreateStructGEP(m_arena_header_type, arena_mem, 0)); // base
        bi.CreateStore(base_ptr, bi.CreateStructGEP(m_arena_header_type, arena_mem, 1)); // bump = base
        auto* limit_ptr = bi.CreateGEP(i8_ty, arena_mem, {arena_alloc_size}, "limit");
        bi.CreateStore(limit_ptr, bi.CreateStructGEP(m_arena_header_type, arena_mem, 2)); // limit
        bi.CreateStore(ConstantInt::get(i64_ty, 0),
            bi.CreateStructGEP(m_arena_header_type, arena_mem, 3)); // object_count = 0
        bi.CreateStore(ConstantPointerNull::get(i8_ptr),
            bi.CreateStructGEP(m_arena_header_type, arena_mem, 6)); // free_list = null

        // Get new arena_id atomically
        auto* old_count = bi.CreateLoad(i64_ty, m_g_gc_arena_count, "old_arena_count");
        auto* new_count = bi.CreateAdd(old_count, ConstantInt::get(i64_ty, 1));
        bi.CreateStore(new_count, m_g_gc_arena_count);
        auto* arena_id = bi.CreateTrunc(old_count, i32_ty, "arena_id");
        bi.CreateStore(arena_id, bi.CreateStructGEP(m_arena_header_type, arena_mem, 4));

        // Link: next_arena = thread's current arena (build per-thread list)
        auto* old_thread_arena = bi.CreateLoad(i8_ptr,
            bi.CreateStructGEP(m_gc_thread_state_type, tls, 4), "old_thread_arena");
        bi.CreateStore(old_thread_arena,
            bi.CreateStructGEP(m_arena_header_type, arena_mem, 5));
        bi.CreateStore(arena_mem, bi.CreateStructGEP(m_gc_thread_state_type, tls, 4));

        // Register in global arenas array
        auto* arenas_ptr = bi.CreateInBoundsGEP(
            ArrayType::get(i8_ptr, 256), m_g_gc_arenas,
            {ConstantInt::get(i64_ty, 0), old_count});
        bi.CreateStore(arena_mem, arenas_ptr);

        // Lazily spawn chaperone thread on first arena creation
        bi.CreateCall(get_func("__ang_gc_chaperone_spawn"), {});

        bi.CreateBr(retry_bb);

        // Retry allocation from the new/recycled arena
        IRBuilder<> br(retry_bb);
        auto* alloc_fn = get_func("__ang_gc_alloc");
        auto* result = br.CreateCall(alloc_fn, {size_arg, type_arg}, "result");
        br.CreateRet(result);
    }

    // ===================================================================
    // __ang_gc_clear_unique(AngaraObject val) -> void
    // Identical to MarkSweepGC.
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
    // Same as MarkSweepGC: if tag == TAG_OBJ, extract ptr and call mark.
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
        // Read barrier: check for forwarding pointer
        auto* resolved = b2.CreateCall(get_func("__ang_gc_read_barrier"), {obj_ptr}, "resolved");
        b2.CreateCall(get_func("__ang_gc_mark"), {resolved});
        b2.CreateBr(done_bb);

        IRBuilder<> b3(done_bb);
        b3.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_mark(i8* obj_ptr) -> void
    // Tri-color marking: same as MarkSweepGC.
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
        auto* meta_gaddr = b.CreateStructGEP(header_ty, obj_ptr, 1);
        auto* meta = b.CreateLoad(i32_ty, meta_gaddr, "meta");
        auto* color = b.CreateAnd(meta, ConstantInt::get(i32_ty, 0xFF), "color");
        auto* is_black = b.CreateICmpEQ(color, ConstantInt::get(i32_ty, COLOR_BLACK));
        b.CreateCondBr(is_black, done_bb, not_black_bb);

        IRBuilder<> bnb(not_black_bb);
        auto* gray_meta = bnb.CreateOr(
            bnb.CreateAnd(meta, ConstantInt::get(i32_ty, ~0xFF)),
            ConstantInt::get(i32_ty, COLOR_GRAY), "gray_meta");
        bnb.CreateStore(gray_meta, bnb.CreateStructGEP(header_ty, obj_ptr, 1));
        bnb.CreateBr(scan_bb);

        IRBuilder<> bs(scan_bb);
        bs.CreateCall(get_func("__ang_gc_scan"), {obj_ptr});
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
    // Type-dispatched traversal: identical to MarkSweepGC.
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

        // OBJ_STRING: no managed children
        { IRBuilder<> bs(string_bb); bs.CreateBr(done_bb); }
        // OBJ_MUTEX: no managed children
        { IRBuilder<> bm(mutex_bb); bm.CreateBr(done_bb); }
        // OBJ_NATIVE_INSTANCE: no managed children
        { IRBuilder<> bn(native_bb); bn.CreateBr(done_bb); }

        // OBJ_LIST: iterate elements, mark each
        {
            IRBuilder<> bls(list_bb);
            auto* count = bls.CreateLoad(i64_ty,
                bls.CreateStructGEP(m_list_type, obj_ptr, 1), "count");
            auto* elems = bls.CreateLoad(i8_ptr,
                bls.CreateStructGEP(m_list_type, obj_ptr, 3), "elems");
            auto* loop_bb = BasicBlock::Create(m_ctx, "list_loop", fn);
            bls.CreateBr(loop_bb);

            IRBuilder<> bll(loop_bb);
            auto* i_phi = bll.CreatePHI(i64_ty, 2, "i");
            i_phi->addIncoming(ConstantInt::get(i64_ty, 0), list_bb);
            auto* cont = bll.CreateICmpSLT(i_phi, count);
            auto* body_bb = BasicBlock::Create(m_ctx, "list_body", fn);
            auto* list_done_bb = BasicBlock::Create(m_ctx, "list_done", fn);
            bll.CreateCondBr(cont, body_bb, list_done_bb);

            IRBuilder<> bb(body_bb);
            auto* elem_ptr = bb.CreateGEP(obj_ty, elems, {i_phi});
            auto* elem = bb.CreateLoad(obj_ty, elem_ptr, "elem");
            bb.CreateCall(get_func("__ang_gc_mark_value"), {elem});
            auto* next_i = bb.CreateAdd(i_phi, ConstantInt::get(i64_ty, 1));
            bb.CreateBr(loop_bb);
            i_phi->addIncoming(next_i, body_bb);

            IRBuilder<> bld(list_done_bb);
            bld.CreateBr(done_bb);
        }

        // OBJ_EXCEPTION: mark the message
        {
            IRBuilder<> be(exception_bb);
            auto* msg = be.CreateLoad(obj_ty,
                be.CreateStructGEP(m_exception_type, obj_ptr, 1), "msg");
            be.CreateCall(get_func("__ang_gc_mark_value"), {msg});
            be.CreateBr(done_bb);
        }

        // OBJ_CLOSURE: iterate env array
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
        {
            auto* thread_done_bb = BasicBlock::Create(m_ctx, "thread_done", fn);
            auto* args_loop_bb = BasicBlock::Create(m_ctx, "args_loop", fn);
            auto* args_body_bb = BasicBlock::Create(m_ctx, "args_body", fn);
            auto* args_done_bb = BasicBlock::Create(m_ctx, "args_done", fn);

            IRBuilder<> bt(thread_bb);
            auto* closure = bt.CreateLoad(obj_ty,
                bt.CreateStructGEP(m_thread_type, obj_ptr, 2), "closure");
            bt.CreateCall(get_func("__ang_gc_mark_value"), {closure});
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

        // OBJ_RECORD (also CLASS, INSTANCE, DATA_INSTANCE, ENUM_INSTANCE)
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

        IRBuilder<> bd(done_bb);
        bd.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_finalize(i8* obj_ptr) -> void
    // Identical to MarkSweepGC: free internal buffers.
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
        auto* closure_bb = BasicBlock::Create(m_ctx, "fin_closure", fn);
        auto* done_bb = BasicBlock::Create(m_ctx, "done", fn);

        IRBuilder<> b(entry);
        auto* obj_ptr = fn->arg_begin();
        auto* type_gaddr = b.CreateStructGEP(header_ty, obj_ptr, 0);
        auto* obj_type = b.CreateLoad(i32_ty, type_gaddr, "obj_type");

        // BUG-9: CLASS/INSTANCE/DATA_INSTANCE/ENUM_INSTANCE share the record
        // layout (header + count + cap + entries) -- the scan switch already
        // treats them as records. Route them through the same finalize path so
        // their entries arrays and strdup'd keys are freed on collection.
        auto* sw = b.CreateSwitch(obj_type, done_bb, 9);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_STRING), string_bb);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_LIST), list_bb);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_RECORD), record_bb);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_CLASS), record_bb);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_INSTANCE), record_bb);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_DATA_INSTANCE), record_bb);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_ENUM_INSTANCE), record_bb);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_NATIVE_INSTANCE), native_bb);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_CLOSURE), closure_bb);

        // OBJ_STRING
        {
            IRBuilder<> bs(string_bb);
            auto* chars_ptr = bs.CreateLoad(i8_ptr,
                bs.CreateStructGEP(m_string_type, obj_ptr, 3), "chars");
            auto* has_chars = bs.CreateICmpNE(chars_ptr, ConstantPointerNull::get(i8_ptr));
            auto* free_chars_bb = BasicBlock::Create(m_ctx, "free_chars", fn);
            bs.CreateCondBr(has_chars, free_chars_bb, done_bb);
            IRBuilder<> bfc(free_chars_bb);
            bfc.CreateCall(free_fn, {chars_ptr});
            bfc.CreateBr(done_bb);
        }

        // OBJ_LIST
        {
            IRBuilder<> bl(list_bb);
            auto* elems_ptr = bl.CreateLoad(PointerType::get(m_ctx, 0),
                bl.CreateStructGEP(m_list_type, obj_ptr, 3), "elems");
            auto* has_elems = bl.CreateICmpNE(elems_ptr, ConstantPointerNull::get(PointerType::get(m_ctx, 0)));
            auto* free_elems_bb = BasicBlock::Create(m_ctx, "free_elems", fn);
            bl.CreateCondBr(has_elems, free_elems_bb, done_bb);
            IRBuilder<> bfe(free_elems_bb);
            bfe.CreateCall(free_fn, {bfe.CreateBitCast(elems_ptr, i8_ptr)});
            bfe.CreateBr(done_bb);
        }

        // OBJ_RECORD
        {
            IRBuilder<> br(record_bb);
            auto* count = br.CreateLoad(i64_ty,
                br.CreateStructGEP(m_record_type, obj_ptr, 1), "count");
            auto* entries = br.CreateLoad(PointerType::get(m_ctx, 0),
                br.CreateStructGEP(m_record_type, obj_ptr, 3), "entries");
            auto* has_entries = br.CreateICmpNE(entries, ConstantPointerNull::get(PointerType::get(m_ctx, 0)));
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
            brb.CreateCall(free_fn, {key});
            auto* next_i = brb.CreateAdd(i_phi, ConstantInt::get(i64_ty, 1));
            brb.CreateBr(record_loop_bb);
            i_phi->addIncoming(next_i, record_body_bb);

            IRBuilder<> brfe(record_free_entries_bb);
            brfe.CreateCall(free_fn, {brfe.CreateBitCast(entries, i8_ptr)});
            brfe.CreateBr(done_bb);
        }

        // OBJ_NATIVE_INSTANCE: call the native finalize callback (sqlite3_close,
        // fd close, ...) and free the strdup'd name. BUG-6 now tracks native
        // instances, so this finalize actually runs.
        {
            IRBuilder<> bn(native_bb);
            // BUG-9: free the strdup'd name (field 3); free(NULL) is safe.
            auto* name = bn.CreateLoad(i8_ptr,
                bn.CreateStructGEP(m_native_instance_type, obj_ptr, 3), "name");
            bn.CreateCall(free_fn, {name});
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

        // OBJ_CLOSURE: free the malloc'd env array (field 4). The captured
        // AngaraObject elements are GC-tracked and scanned separately, so only
        // the backing array is released here.
        {
            IRBuilder<> bcl(closure_bb);
            auto* env = bcl.CreateLoad(PointerType::get(m_ctx, 0),
                bcl.CreateStructGEP(m_closure_type, obj_ptr, 4), "env");
            bcl.CreateCall(free_fn, {bcl.CreateBitCast(env, i8_ptr)});
            bcl.CreateBr(done_bb);
        }

        IRBuilder<> bd(done_bb);
        bd.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_sweep() -> void
    // Linked-list sweep using forward field (index 2) as next pointer.
    // The allocation list head is stored in __ang_gc_arenas[0].
    // ===================================================================
    {
        auto* fn = get_func("__ang_gc_sweep");
        // The pre-declaration has no body — erase any accidental blocks
        while (!fn->empty())
            fn->back().eraseFromParent();

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

        // Allocation linked-list head
        IRBuilder<> b(entry);
        auto* head = b.CreateLoad(i8_ptr, m_g_gc_alloc_list, "head");
        b.CreateBr(loop_bb);

        IRBuilder<> bl(loop_bb);
        auto* curr_phi = bl.CreatePHI(i8_ptr, 2, "curr");
        curr_phi->addIncoming(head, entry);
        auto* prev_phi = bl.CreatePHI(i8_ptr, 2, "prev");
        prev_phi->addIncoming(ConstantPointerNull::get(i8_ptr), entry);
        auto* is_null = bl.CreateICmpEQ(curr_phi, ConstantPointerNull::get(i8_ptr));
        bl.CreateCondBr(is_null, done_bb, check_color_bb);

        IRBuilder<> bcc(check_color_bb);
        auto* meta_gaddr = bcc.CreateStructGEP(header_ty, curr_phi, 1);
        auto* meta = bcc.CreateLoad(i32_ty, meta_gaddr, "meta");
        auto* color = bcc.CreateAnd(meta, ConstantInt::get(i32_ty, 0xFF), "color");
        auto* is_white = bcc.CreateICmpEQ(color, ConstantInt::get(i32_ty, COLOR_WHITE));
        bcc.CreateCondBr(is_white, is_white_bb, is_black_bb);

        IRBuilder<> bw(is_white_bb);
        auto* pinned_bit = bw.CreateAnd(meta, ConstantInt::get(i32_ty, 1 << PINNED_SHIFT), "pinned");
        auto* is_pinned = bw.CreateICmpNE(pinned_bit, ConstantInt::get(i32_ty, 0));
        bw.CreateCondBr(is_pinned, is_black_bb, check_pinned_bb);

        IRBuilder<> bcp(check_pinned_bb);
        auto* next_ptr = bcp.CreateLoad(i8_ptr,
            bcp.CreateStructGEP(header_ty, curr_phi, 2), "next");
        auto* prev_is_null = bcp.CreateICmpEQ(prev_phi, ConstantPointerNull::get(i8_ptr));
        bcp.CreateCondBr(prev_is_null, unlink_head_bb, unlink_mid_bb);

        IRBuilder<> buh(unlink_head_bb);
        buh.CreateStore(next_ptr, m_g_gc_alloc_list);
        buh.CreateBr(free_obj_bb);

        IRBuilder<> bum(unlink_mid_bb);
        bum.CreateStore(next_ptr, bum.CreateStructGEP(header_ty, prev_phi, 2));
        bum.CreateBr(free_obj_bb);

        IRBuilder<> bf(free_obj_bb);
        bf.CreateCall(get_func("__ang_gc_finalize"), {curr_phi});
        // Stats
        auto* frees = bf.CreateLoad(i64_ty, m_g_gc_total_frees, "frees");
        bf.CreateStore(bf.CreateAdd(frees, ConstantInt::get(i64_ty, 1)), m_g_gc_total_frees);
        bf.CreateBr(advance_bb);

        IRBuilder<> bbk(is_black_bb);
        auto* next_ptr2 = bbk.CreateLoad(i8_ptr,
            bbk.CreateStructGEP(header_ty, curr_phi, 2), "next2");
        auto* reset_meta = bbk.CreateAnd(meta, ConstantInt::get(i32_ty, ~0xFF), "reset_meta");
        bbk.CreateStore(reset_meta, bbk.CreateStructGEP(header_ty, curr_phi, 1));
        bbk.CreateBr(advance_bb);

        IRBuilder<> ba(advance_bb);
        auto* next_phi = ba.CreatePHI(i8_ptr, 2, "next");
        next_phi->addIncoming(next_ptr, free_obj_bb);
        next_phi->addIncoming(next_ptr2, is_black_bb);
        auto* prev_next = ba.CreatePHI(i8_ptr, 2, "prev_next");
        prev_next->addIncoming(prev_phi, free_obj_bb);
        prev_next->addIncoming(curr_phi, is_black_bb);
        curr_phi->addIncoming(next_phi, advance_bb);
        prev_phi->addIncoming(prev_next, advance_bb);
        ba.CreateBr(loop_bb);

        IRBuilder<> bd(done_bb);
        bd.CreateRetVoid();
    }

    // ===================================================================
    // Patch __ang_gc_alloc: link returned objects into the allocation list.
    // The list head is __ang_gc_alloc_list; forward field (index 2) = next.
    //
    // IMPORTANT: Skip returns whose value comes from __ang_gc_alloc_slow.
    // alloc_slow calls __ang_gc_alloc recursively, so the object is already
    // linked by the recursive call. Double-linking creates a circular
    // self-referencing node that corrupts the sweep.
    // ===================================================================
    {
        auto* fn = get_func("__ang_gc_alloc");
        auto* alloc_slow_fn = get_func("__ang_gc_alloc_slow");
        // Collect return instructions, excluding the slow path
        std::vector<ReturnInst*> rets;
        for (auto& bb : *fn) {
            if (auto* ret = dyn_cast<ReturnInst>(bb.getTerminator())) {
                if (!ret->getReturnValue()) continue;
                // Skip returns that delegate to alloc_slow — the recursive
                // call already links the object into the alloc list.
                if (auto* ci = dyn_cast<CallInst>(ret->getReturnValue())) {
                    if (ci->getCalledFunction() == alloc_slow_fn) continue;
                }
                rets.push_back(ret);
            }
        }
        for (auto* ret : rets) {
            auto* ret_val = ret->getReturnValue();
            auto* link_bb = BasicBlock::Create(m_ctx, "link", fn);
            IRBuilder<> bl(link_bb);
            auto* old_head = bl.CreateLoad(i8_ptr, m_g_gc_alloc_list, "old_head");
            bl.CreateStore(old_head, bl.CreateStructGEP(header_ty, ret_val, 2));
            bl.CreateStore(ret_val, m_g_gc_alloc_list);
            bl.CreateRet(ret_val);
            // Replace the return with a branch to link_bb
            auto* parent = ret->getParent();
            ret->eraseFromParent();
            IRBuilder<> br(parent);
            br.CreateBr(link_bb);
        }
    }

    // ===================================================================
    // __ang_gc_mark_roots() -> void
    // Identical to MarkSweepGC.
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

        IRBuilder<> btl(thread_loop_bb);
        auto* thread_phi = btl.CreatePHI(i8_ptr, 2, "thread");
        thread_phi->addIncoming(threads, entry);
        auto* thread_done = btl.CreateICmpEQ(thread_phi, ConstantPointerNull::get(i8_ptr));
        btl.CreateCondBr(thread_done, done_bb, frame_setup_bb);

        IRBuilder<> bfs(frame_setup_bb);
        auto* root_frames = bfs.CreateLoad(i8_ptr,
            bfs.CreateStructGEP(m_gc_thread_state_type, thread_phi, 2), "root_frames");
        bfs.CreateBr(frame_loop_bb);

        IRBuilder<> bfl(frame_loop_bb);
        auto* frame_phi = bfl.CreatePHI(i8_ptr, 2, "frame");
        frame_phi->addIncoming(root_frames, frame_setup_bb);
        auto* frame_done = bfl.CreateICmpEQ(frame_phi, ConstantPointerNull::get(i8_ptr));
        bfl.CreateCondBr(frame_done, next_thread_bb, slot_loop_bb);

        IRBuilder<> bsl(slot_loop_bb);
        auto* i_phi = bsl.CreatePHI(i64_ty, 2, "slot_i");
        i_phi->addIncoming(ConstantInt::get(i64_ty, 0), frame_loop_bb);
        auto* count = bsl.CreateLoad(i32_ty,
            bsl.CreateStructGEP(m_gc_root_frame_type, frame_phi, 1), "slot_count");
        auto* count_ext = bsl.CreateZExt(count, i64_ty, "count_ext");
        auto* has_more = bsl.CreateICmpSLT(i_phi, count_ext);
        bsl.CreateCondBr(has_more, slot_body_bb, next_frame_bb);

        IRBuilder<> bsb(slot_body_bb);
        auto* slot_addr = bsb.CreateGEP(m_gc_root_frame_type, frame_phi,
            {ConstantInt::get(i32_ty, 0), ConstantInt::get(i32_ty, 2), i_phi});
        auto* obj_ptr = bsb.CreateLoad(i8_ptr, slot_addr, "obj_ptr");
        auto* obj_val = bsb.CreateLoad(obj_ty, obj_ptr, "obj_val");
        bsb.CreateCall(get_func("__ang_gc_mark_value"), {obj_val});
        auto* next_i = bsb.CreateAdd(i_phi, ConstantInt::get(i64_ty, 1));
        bsb.CreateBr(slot_loop_bb);
        i_phi->addIncoming(next_i, slot_body_bb);

        IRBuilder<> bnf(next_frame_bb);
        auto* prev_frame = bnf.CreateLoad(i8_ptr,
            bnf.CreateStructGEP(m_gc_root_frame_type, frame_phi, 0), "prev_frame");
        bnf.CreateBr(frame_loop_bb);
        frame_phi->addIncoming(prev_frame, next_frame_bb);

        IRBuilder<> bnt(next_thread_bb);
        auto* next_thread = bnt.CreateLoad(i8_ptr,
            bnt.CreateStructGEP(m_gc_thread_state_type, thread_phi, 1), "next_thread");
        bnt.CreateBr(thread_loop_bb);
        thread_phi->addIncoming(next_thread, next_thread_bb);

        IRBuilder<> bd(done_bb);
        bd.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_recycle_arenas() -> void
    // Post-sweep pass: iterate all arenas, count live objects, and
    // recycle empty arenas back to the free pool.
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {}, false);
        auto* fn = cast<Function>(m_module.getOrInsertFunction("__ang_gc_recycle_arenas", fn_ty).getCallee());
        fn->setLinkage(Function::InternalLinkage);
        fn->setDSOLocal(true);
        fn->addFnAttr(llvm::Attribute::NoInline);
        // Erase any blocks from pre-declaration or prior use
        while (!fn->empty())
            fn->back().eraseFromParent();
        m_fn_gc_recycle_arenas = FunctionCallee(fn);

        // Strategy: walk the alloc list, extract arena_id from each live object,
        // build a per-arena live count. Then iterate arenas and recycle empty ones.
        // We use a simple i64 array on the stack (256 entries) for per-arena counts.

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* zero_loop_bb = BasicBlock::Create(m_ctx, "zero_loop", fn);
        auto* zero_body_bb = BasicBlock::Create(m_ctx, "zero_body", fn);
        auto* count_loop_bb = BasicBlock::Create(m_ctx, "count_loop", fn);
        auto* count_body_bb = BasicBlock::Create(m_ctx, "count_body", fn);
        auto* arena_loop_bb = BasicBlock::Create(m_ctx, "arena_loop", fn);
        auto* arena_body_bb = BasicBlock::Create(m_ctx, "arena_body", fn);
        auto* recycle_bb = BasicBlock::Create(m_ctx, "recycle", fn);
        auto* next_arena_bb = BasicBlock::Create(m_ctx, "next_arena", fn);
        auto* done_bb = BasicBlock::Create(m_ctx, "done", fn);

        IRBuilder<> b(entry);
        // Allocate per-arena count array on the stack (256 * 8 bytes)
        auto* counts = b.CreateAlloca(ArrayType::get(i64_ty, 256), ConstantInt::get(i64_ty, 1), "counts");
        auto* alloc_list_head = b.CreateLoad(i8_ptr, m_g_gc_alloc_list, "head");
        b.CreateBr(zero_loop_bb);

        // Zero the counts array
        IRBuilder<> bzl(zero_loop_bb);
        auto* zi_phi = bzl.CreatePHI(i64_ty, 2, "zi");
        zi_phi->addIncoming(ConstantInt::get(i64_ty, 0), entry);
        auto* zi_end = bzl.CreateICmpSLT(zi_phi, ConstantInt::get(i64_ty, 256));
        bzl.CreateCondBr(zi_end, zero_body_bb, count_loop_bb);

        IRBuilder<> bzb(zero_body_bb);
        bzb.CreateStore(ConstantInt::get(i64_ty, 0),
            bzb.CreateInBoundsGEP(ArrayType::get(i64_ty, 256), counts,
                {ConstantInt::get(i64_ty, 0), zi_phi}));
        auto* next_zi = bzb.CreateAdd(zi_phi, ConstantInt::get(i64_ty, 1));
        zi_phi->addIncoming(next_zi, zero_body_bb);
        bzb.CreateBr(zero_loop_bb);

        // Walk alloc list, increment per-arena counts
        IRBuilder<> bcl(count_loop_bb);
        auto* cur_phi = bcl.CreatePHI(i8_ptr, 2, "cur");
        cur_phi->addIncoming(alloc_list_head, zero_loop_bb);
        auto* is_null = bcl.CreateICmpEQ(cur_phi, ConstantPointerNull::get(i8_ptr));
        bcl.CreateCondBr(is_null, arena_loop_bb, count_body_bb);

        IRBuilder<> bcb(count_body_bb);
        auto* meta = bcb.CreateLoad(i32_ty,
            bcb.CreateStructGEP(header_ty, cur_phi, 1), "meta");
        auto* arena_id_val = bcb.CreateLShr(
            bcb.CreateAnd(meta, ConstantInt::get(i32_ty, 0xFF000000)),
            ConstantInt::get(i32_ty, 24), "arena_id");
        auto* arena_id_ext = bcb.CreateZExt(arena_id_val, i64_ty, "arena_id_ext");
        auto* count_addr = bcb.CreateInBoundsGEP(ArrayType::get(i64_ty, 256), counts,
            {ConstantInt::get(i64_ty, 0), arena_id_ext});
        auto* old_count = bcb.CreateLoad(i64_ty, count_addr, "old_count");
        bcb.CreateStore(bcb.CreateAdd(old_count, ConstantInt::get(i64_ty, 1)), count_addr);
        auto* next_obj = bcb.CreateLoad(i8_ptr,
            bcb.CreateStructGEP(header_ty, cur_phi, 2), "next_obj");
        cur_phi->addIncoming(next_obj, count_body_bb);
        bcb.CreateBr(count_loop_bb);

        // Iterate arenas, check counts, recycle empty ones
        IRBuilder<> bal(arena_loop_bb);
        auto* i_phi = bal.CreatePHI(i64_ty, 2, "i");
        i_phi->addIncoming(ConstantInt::get(i64_ty, 0), count_loop_bb);
        auto* arena_count = bal.CreateLoad(i64_ty, m_g_gc_arena_count, "arena_count");
        auto* at_end = bal.CreateICmpSLT(i_phi, arena_count);
        bal.CreateCondBr(at_end, arena_body_bb, done_bb);

        IRBuilder<> bab(arena_body_bb);
        auto* arena_addr = bab.CreateInBoundsGEP(
            ArrayType::get(i8_ptr, 256), m_g_gc_arenas,
            {ConstantInt::get(i64_ty, 0), i_phi});
        auto* arena = bab.CreateLoad(i8_ptr, arena_addr, "arena");
        auto* is_null2 = bab.CreateICmpEQ(arena, ConstantPointerNull::get(i8_ptr));
        bab.CreateCondBr(is_null2, next_arena_bb, recycle_bb);

        IRBuilder<> brc(recycle_bb);
        // Load count for this arena
        auto* live_count = brc.CreateLoad(i64_ty,
            brc.CreateInBoundsGEP(ArrayType::get(i64_ty, 256), counts,
                {ConstantInt::get(i64_ty, 0), i_phi}), "live_count");
        // Update arena.object_count
        brc.CreateStore(live_count, brc.CreateStructGEP(arena_ty, arena, 3));
        auto* is_empty = brc.CreateICmpEQ(live_count, ConstantInt::get(i64_ty, 0));
        auto* do_recycle_bb = BasicBlock::Create(m_ctx, "do_recycle", fn);
        brc.CreateCondBr(is_empty, do_recycle_bb, next_arena_bb);

        IRBuilder<> bdr(do_recycle_bb);
        auto* old_free = bdr.CreateLoad(i8_ptr, m_g_gc_arena_free, "old_free");
        bdr.CreateStore(old_free, bdr.CreateStructGEP(arena_ty, arena, 5));
        // Reset bump to base
        auto* base = bdr.CreateLoad(i8_ptr,
            bdr.CreateStructGEP(arena_ty, arena, 0), "base");
        bdr.CreateStore(base, bdr.CreateStructGEP(arena_ty, arena, 1));
        // Clear free_list
        bdr.CreateStore(ConstantPointerNull::get(i8_ptr),
            bdr.CreateStructGEP(arena_ty, arena, 6));
        bdr.CreateStore(arena, m_g_gc_arena_free);
        // Null out the arena slot
        bdr.CreateStore(ConstantPointerNull::get(i8_ptr), arena_addr);
        bdr.CreateBr(next_arena_bb);

        IRBuilder<> bna(next_arena_bb);
        auto* next_i = bna.CreateAdd(i_phi, ConstantInt::get(i64_ty, 1));
        i_phi->addIncoming(next_i, next_arena_bb);
        bna.CreateBr(arena_loop_bb);

        IRBuilder<> bd(done_bb);
        bd.CreateRetVoid();
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
        b.CreateCall(get_func("__ang_gc_mark_roots"), {});
        b.CreateCall(get_func("__ang_gc_sweep"), {});
        // Run one chaperone compaction step (synchronous, after sweep)
        b.CreateCall(get_func("__ang_gc_chaperone_step"), {});
        b.CreateCall(get_func("__ang_gc_recycle_arenas"), {});
        auto* cols = b.CreateLoad(i64_ty, m_g_gc_collections, "cols");
        b.CreateStore(b.CreateAdd(cols, ConstantInt::get(i64_ty, 1)), m_g_gc_collections);
        b.CreateStore(ConstantInt::get(i64_ty, 0), m_g_gc_total_count);
        b.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_push_frame(i8* frame_ptr) -> void
    // Identical to MarkSweepGC.
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
        auto* root_frames = bht.CreateLoad(i8_ptr,
            bht.CreateStructGEP(m_gc_thread_state_type, tls, 2), "root_frames");
        bht.CreateStore(root_frames,
            bht.CreateStructGEP(m_gc_root_frame_type, frame_ptr, 0));
        bht.CreateStore(frame_ptr,
            bht.CreateStructGEP(m_gc_thread_state_type, tls, 2));
        bht.CreateBr(done_bb);

        IRBuilder<> bd(done_bb);
        bd.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_pop_frame() -> void
    // Identical to MarkSweepGC.
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
    // Extended: also allocate initial arena.
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {i8_ptr}, false);
        auto* fn = createRuntimeFunc("__ang_gc_thread_register", fn_ty);
        m_fn_gc_thread_register = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* state = fn->arg_begin();

        b.CreateStore(state,
            b.CreateStructGEP(m_gc_thread_state_type, state, 0));
        b.CreateStore(ConstantPointerNull::get(i8_ptr),
            b.CreateStructGEP(m_gc_thread_state_type, state, 1));
        b.CreateStore(ConstantPointerNull::get(i8_ptr),
            b.CreateStructGEP(m_gc_thread_state_type, state, 2));
        b.CreateStore(ConstantInt::getFalse(m_ctx),
            b.CreateStructGEP(m_gc_thread_state_type, state, 3));
        b.CreateStore(ConstantPointerNull::get(i8_ptr),
            b.CreateStructGEP(m_gc_thread_state_type, state, 4)); // current_arena = null

        b.CreateStore(state, m_g_gc_thread_state_tls);

        auto* old_head = b.CreateLoad(i8_ptr, m_g_gc_threads, "old_threads");
        b.CreateStore(old_head,
            b.CreateStructGEP(m_gc_thread_state_type, state, 1));
        b.CreateStore(state, m_g_gc_threads);

        b.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_thread_unregister(i8* state_ptr) -> void
    // Identical to MarkSweepGC.
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
        auto* is_head = b.CreateICmpEQ(head, state);
        b.CreateCondBr(is_head, unlink_head_bb, walk_bb);

        IRBuilder<> buh(unlink_head_bb);
        auto* next = buh.CreateLoad(i8_ptr,
            buh.CreateStructGEP(m_gc_thread_state_type, head, 1), "next");
        buh.CreateStore(next, m_g_gc_threads);
        buh.CreateBr(done_bb);

        IRBuilder<> bw(walk_bb);
        auto* curr_phi = bw.CreatePHI(i8_ptr, 2, "curr");
        curr_phi->addIncoming(head, entry);
        auto* curr_null = bw.CreateICmpEQ(curr_phi, ConstantPointerNull::get(i8_ptr));
        bw.CreateCondBr(curr_null, done_bb, check_next_bb);

        IRBuilder<> bcn(check_next_bb);
        auto* curr_next = bcn.CreateLoad(i8_ptr,
            bcn.CreateStructGEP(m_gc_thread_state_type, curr_phi, 1), "curr_next");
        auto* found = bcn.CreateICmpEQ(curr_next, state);
        bcn.CreateCondBr(found, unlink_mid_bb, walk_bb);
        curr_phi->addIncoming(curr_next, check_next_bb);

        IRBuilder<> bum(unlink_mid_bb);
        auto* state_next = bum.CreateLoad(i8_ptr,
            bum.CreateStructGEP(m_gc_thread_state_type, state, 1), "state_next");
        bum.CreateStore(state_next,
            bum.CreateStructGEP(m_gc_thread_state_type, curr_phi, 1));
        bum.CreateBr(done_bb);

        IRBuilder<> bd(done_bb);
        bd.CreateStore(ConstantPointerNull::get(i8_ptr), m_g_gc_thread_state_tls);
        bd.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_safepoint() -> void
    // Identical to MarkSweepGC.
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

        IRBuilder<> bsw(set_wait_bb);
        auto* tls = bsw.CreateLoad(i8_ptr, m_g_gc_thread_state_tls, "tls");
        auto* tls_ok = bsw.CreateICmpNE(tls, ConstantPointerNull::get(i8_ptr));
        bsw.CreateCondBr(tls_ok, do_set_bb, spin_bb);

        IRBuilder<> bds(do_set_bb);
        bds.CreateStore(ConstantInt::getTrue(m_ctx),
            bds.CreateStructGEP(m_gc_thread_state_type, tls, 3));
        bds.CreateBr(spin_bb);

        IRBuilder<> bspin(spin_bb);
        auto* still_running = bspin.CreateLoad(i1_ty, m_g_gc_running, "still_running");
        bspin.CreateCondBr(still_running, spin_bb, resume_bb);

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
    // __ang_gc_pin / __ang_gc_unpin
    // Identical to MarkSweepGC.
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
        auto* obj_ptr = b2.CreateIntToPtr(b2.CreateBitCast(payload, i64_ty), i8_ptr, "obj_ptr");
        auto* meta_gaddr = b2.CreateStructGEP(header_ty, obj_ptr, 1);
        auto* meta = b2.CreateLoad(i32_ty, meta_gaddr, "meta");
        b2.CreateStore(b2.CreateOr(meta, ConstantInt::get(i32_ty, 1 << PINNED_SHIFT)), meta_gaddr);
        b2.CreateBr(done_bb);

        IRBuilder<> bd(done_bb);
        bd.CreateRetVoid();
    }
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
        auto* obj_ptr = b2.CreateIntToPtr(b2.CreateBitCast(payload, i64_ty), i8_ptr, "obj_ptr");
        auto* meta_gaddr = b2.CreateStructGEP(header_ty, obj_ptr, 1);
        auto* meta = b2.CreateLoad(i32_ty, meta_gaddr, "meta");
        b2.CreateStore(b2.CreateAnd(meta, ConstantInt::get(i32_ty, ~(1 << PINNED_SHIFT))), meta_gaddr);
        b2.CreateBr(done_bb);

        IRBuilder<> bd(done_bb);
        bd.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_read_barrier(i8* obj_ptr) -> i8*
    // Stage 1: identity (no relocation yet). Returns input pointer.
    // Stage 2: checks FORWARDED color and follows forwarding pointer.
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(i8_ptr, {i8_ptr}, false);
        auto* fn = createRuntimeFunc("__ang_gc_read_barrier", fn_ty);
        m_fn_gc_read_barrier = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* forwarded_bb = BasicBlock::Create(m_ctx, "forwarded", fn);
        auto* done_bb = BasicBlock::Create(m_ctx, "done", fn);

        IRBuilder<> b(entry);
        auto* obj_ptr = fn->arg_begin();

        // Load color from meta field
        auto* meta_gaddr = b.CreateStructGEP(header_ty, obj_ptr, 1);
        auto* meta = b.CreateLoad(i32_ty, meta_gaddr, "meta");
        auto* color = b.CreateAnd(meta, ConstantInt::get(i32_ty, 0xFF), "color");
        auto* is_forwarded = b.CreateICmpEQ(color, ConstantInt::get(i32_ty, COLOR_FORWARDED));
        b.CreateCondBr(is_forwarded, forwarded_bb, done_bb);

        // Follow forwarding pointer
        IRBuilder<> bf(forwarded_bb);
        auto* fwd = bf.CreateLoad(i8_ptr,
            bf.CreateStructGEP(header_ty, obj_ptr, 2), "fwd");
        bf.CreateBr(done_bb);

        // Return resolved pointer
        IRBuilder<> bd(done_bb);
        auto* result = bd.CreatePHI(i8_ptr, 2, "result");
        result->addIncoming(obj_ptr, entry);
        result->addIncoming(fwd, forwarded_bb);
        bd.CreateRet(result);
    }

    // ===================================================================
    // __ang_gc_print_stats() -> void
    // Extended with arena count.
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {}, false);
        auto* fn = createRuntimeFunc("__ang_gc_print_stats", fn_ty);
        m_fn_gc_print_stats = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);

        auto* cols = b.CreateLoad(i64_ty, m_g_gc_collections, "cols");
        auto* allocs = b.CreateLoad(i64_ty, m_g_gc_total_allocs, "allocs");
        auto* frees = b.CreateLoad(i64_ty, m_g_gc_total_frees, "frees");
        auto* live = b.CreateSub(allocs, frees, "live");
        auto* arenas = b.CreateLoad(i64_ty, m_g_gc_arena_count, "arenas");

        auto* printf_ty = FunctionType::get(i32_ty, {PointerType::get(m_ctx, 0)}, true);
        auto* printf_fn = cast<Function>(
            m_module.getOrInsertFunction("printf", printf_ty).getCallee());

        auto* f1 = b.CreateGlobalStringPtr("[ChaperoneGC] collections: %lld | ", "f1");
        b.CreateCall(printf_fn, {f1, cols});
        auto* f2 = b.CreateGlobalStringPtr("allocs: %lld | ", "f2");
        b.CreateCall(printf_fn, {f2, allocs});
        auto* f3 = b.CreateGlobalStringPtr("freed: %lld | ", "f3");
        b.CreateCall(printf_fn, {f3, frees});
        auto* f4 = b.CreateGlobalStringPtr("live: %lld | ", "f4");
        b.CreateCall(printf_fn, {f4, live});
        auto* f5 = b.CreateGlobalStringPtr("arenas: %lld\n", "f5");
        b.CreateCall(printf_fn, {f5, arenas});

        b.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_obj_size(i8* obj_ptr) -> i64
    // Returns the 16-aligned allocation size for an object based on its
    // ObjHeader.type field. Used by compute_energy, chaperone_step,
    // relocate, and arena walking.
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(i64_ty, {i8_ptr}, false);
        auto* fn = createRuntimeFunc("__ang_gc_obj_size", fn_ty);
        m_fn_gc_obj_size = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* obj_ptr = fn->arg_begin();
        auto* obj_type_val = b.CreateLoad(i32_ty,
            b.CreateStructGEP(header_ty, obj_ptr, 0), "obj_type");
        // Only types with non-48 sizes need explicit checks
        auto* is_exception = b.CreateICmpEQ(obj_type_val, ConstantInt::get(i32_ty, OBJ_EXCEPTION));
        auto* is_thread = b.CreateICmpEQ(obj_type_val, ConstantInt::get(i32_ty, OBJ_THREAD));
        auto* is_mutex = b.CreateICmpEQ(obj_type_val, ConstantInt::get(i32_ty, OBJ_MUTEX));
        auto* sz1 = b.CreateSelect(is_exception, ConstantInt::get(i64_ty, 32), ConstantInt::get(i64_ty, 48));
        auto* sz2 = b.CreateSelect(is_thread, ConstantInt::get(i64_ty, 64), sz1);
        auto* obj_size = b.CreateSelect(is_mutex, ConstantInt::get(i64_ty, 80), sz2);
        b.CreateRet(obj_size);
    }

    // ===================================================================
    // __ang_gc_compute_energy(i8* arena_ptr) -> f64
    // Computes the free-energy of an arena: E = fragmentation score.
    // In the biological analogy, this measures how "misfolded" the arena is.
    // High energy = fragmented, low energy = compact and healthy.
    //
    // Energy = w_f * (1 - live_ratio)
    //   where live_ratio = live_object_bytes / used_arena_bytes
    //   w_f = 0.4 (fragmentation weight from CHAPERONE_GC.md)
    //
    // For simplicity, we approximate live_object_bytes by counting non-white
    // objects and multiplying by a fixed average size estimate.
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(f64_ty, {i8_ptr}, false);
        auto* fn = createRuntimeFunc("__ang_gc_compute_energy", fn_ty);
        m_fn_gc_compute_energy = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* walk_bb = BasicBlock::Create(m_ctx, "walk", fn);
        auto* body_bb = BasicBlock::Create(m_ctx, "body", fn);
        auto* next_bb = BasicBlock::Create(m_ctx, "next_obj", fn);
        auto* done_bb = BasicBlock::Create(m_ctx, "done", fn);

        IRBuilder<> b(entry);
        auto* arena = fn->arg_begin();

        // Load base and bump
        auto* base = b.CreateLoad(i8_ptr,
            b.CreateStructGEP(arena_ty, arena, 0), "base");
        auto* bump = b.CreateLoad(i8_ptr,
            b.CreateStructGEP(arena_ty, arena, 1), "bump");

        // used_bytes = bump - base
        auto* used_bytes_i64 = b.CreatePtrDiff(i64_ty, bump, base, "used_bytes");
        auto* used_bytes_f = b.CreateSIToFP(used_bytes_i64, f64_ty, "used_f");

        // Walk objects from base to bump, count live ones
        // Each object: read header at current ptr, get obj_type (field 0),
        // get meta (field 1), extract color. If not white, it's live.
        // Advance by aligned_size (we align to 16 bytes).
        // For simplicity, we estimate: live_count * 48 (average obj size)
        b.CreateBr(walk_bb);

        IRBuilder<> bw(walk_bb);
        auto* cur_phi = bw.CreatePHI(i8_ptr, 2, "cur");
        cur_phi->addIncoming(base, entry);
        auto* live_phi = bw.CreatePHI(i64_ty, 2, "live_count");
        live_phi->addIncoming(ConstantInt::get(i64_ty, 0), entry);
        auto* live_bytes_phi = bw.CreatePHI(i64_ty, 2, "live_bytes");
        live_bytes_phi->addIncoming(ConstantInt::get(i64_ty, 0), entry);
        auto* at_end = bw.CreateICmpUGE(cur_phi, bump);
        bw.CreateCondBr(at_end, done_bb, body_bb);

        IRBuilder<> bb(body_bb);
        auto* meta = bb.CreateLoad(i32_ty,
            bb.CreateStructGEP(header_ty, cur_phi, 1), "meta");
        auto* color = bb.CreateAnd(meta, ConstantInt::get(i32_ty, 0xFF), "color");
        auto* is_white = bb.CreateICmpEQ(color, ConstantInt::get(i32_ty, COLOR_WHITE));
        // Get actual object size from type field
        auto* stride = bb.CreateCall(get_func("__ang_gc_obj_size"), {cur_phi}, "stride");
        // If not white, increment live count and accumulate live bytes
        auto* new_live = bb.CreateSelect(is_white, live_phi,
            bb.CreateAdd(live_phi, ConstantInt::get(i64_ty, 1)), "new_live");
        auto* new_live_bytes = bb.CreateSelect(is_white, live_bytes_phi,
            bb.CreateAdd(live_bytes_phi, stride), "new_live_bytes");
        auto* next_ptr = bb.CreateGEP(i8_ty, cur_phi, {stride}, "next_ptr");
        bb.CreateBr(next_bb);

        IRBuilder<> bn(next_bb);
        cur_phi->addIncoming(next_ptr, next_bb);
        live_phi->addIncoming(new_live, next_bb);
        live_bytes_phi->addIncoming(new_live_bytes, next_bb);
        bn.CreateBr(walk_bb);

        // Compute energy from live ratio
        IRBuilder<> bd(done_bb);
        auto* live_bytes_f = bd.CreateSIToFP(live_bytes_phi, f64_ty, "live_bytes_f");
        // Ratio: live_bytes / used_bytes
        auto* used_safe = bd.CreateSelect(
            bd.CreateFCmpOEQ(used_bytes_f, ConstantFP::get(f64_ty, 0.0)),
            ConstantFP::get(f64_ty, 1.0), used_bytes_f, "used_safe");
        auto* ratio = bd.CreateFDiv(live_bytes_f, used_safe, "ratio");
        // Clamp ratio to [0, 1]
        auto* clamped = bd.CreateSelect(bd.CreateFCmpOGT(ratio, ConstantFP::get(f64_ty, 1.0)),
            ConstantFP::get(f64_ty, 1.0), ratio, "clamped");
        // Energy = 0.4 * (1 - ratio)
        auto* energy = bd.CreateFMul(
            ConstantFP::get(f64_ty, 0.4),
            bd.CreateFSub(ConstantFP::get(f64_ty, 1.0), clamped),
            "energy");
        bd.CreateRet(energy);
    }

    // ===================================================================
    // __ang_gc_relocate(i8* old_ptr, i8* new_ptr) -> i8*
    // Moves an object from old_ptr to new_ptr using memcpy,
    // then sets the forwarding pointer atomically.
    //
    // In the biological analogy, this is the chaperone physically moving
    // a protein residue from one region to another — the forwarding pointer
    // acts as a "molecular address change" notification.
    //
    // Returns new_ptr (the new location).
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(i8_ptr, {i8_ptr, i8_ptr}, false);
        auto* fn = createRuntimeFunc("__ang_gc_relocate", fn_ty);
        m_fn_gc_relocate = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* old_ptr = fn->arg_begin();
        auto* new_ptr = fn->arg_begin() + 1;

        // Determine object size via helper (returns 16-aligned allocation size)
        auto* obj_size = b.CreateCall(get_func("__ang_gc_obj_size"), {old_ptr}, "obj_size");

        // memcpy(new_ptr, old_ptr, size)
        auto* memcpy_fn = m_module.getFunction("memcpy");
        b.CreateCall(memcpy_fn, {new_ptr, old_ptr, obj_size});

        // Set forwarding: old->forward = new_ptr
        b.CreateStore(new_ptr, b.CreateStructGEP(header_ty, old_ptr, 2));

        // Set old color to FORWARDED
        auto* meta_addr = b.CreateStructGEP(header_ty, old_ptr, 1);
        auto* old_meta = b.CreateLoad(i32_ty, meta_addr, "old_meta");
        auto* fwd_meta = b.CreateOr(
            b.CreateAnd(old_meta, ConstantInt::get(i32_ty, ~0xFF)),
            ConstantInt::get(i32_ty, COLOR_FORWARDED), "fwd_meta");
        b.CreateStore(fwd_meta, meta_addr);

        b.CreateRet(new_ptr);
    }

    // ===================================================================
    // __ang_gc_update_ref_in_value(i8* val_addr, i64 old_i64, i64 new_i64) -> void
    // Given a pointer to an AngaraObject, if its tag is TAG_OBJ and its
    // payload matches old_i64, patch it to new_i64.
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {i8_ptr, i64_ty, i64_ty}, false);
        auto* fn = createRuntimeFunc("__ang_gc_update_ref_in_value", fn_ty);
        m_fn_gc_update_ref_in_value = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* is_obj_bb = BasicBlock::Create(m_ctx, "is_obj", fn);
        auto* patch_bb = BasicBlock::Create(m_ctx, "patch", fn);
        auto* done_bb = BasicBlock::Create(m_ctx, "done", fn);

        IRBuilder<> b(entry);
        auto* val_addr = fn->arg_begin();
        auto* old_i64 = fn->arg_begin() + 1;
        auto* new_i64 = fn->arg_begin() + 2;
        auto* tag = b.CreateLoad(i32_ty, val_addr, "tag");
        auto* is_obj = b.CreateICmpEQ(tag, ConstantInt::get(i32_ty, TAG_OBJ));
        b.CreateCondBr(is_obj, is_obj_bb, done_bb);

        IRBuilder<> bio(is_obj_bb);
        auto* payload_addr = bio.CreateStructGEP(obj_ty, val_addr, 1);
        auto* payload = bio.CreateLoad(i64_ty, payload_addr, "payload");
        auto* matches = bio.CreateICmpEQ(payload, old_i64);
        bio.CreateCondBr(matches, patch_bb, done_bb);

        IRBuilder<> bp(patch_bb);
        bp.CreateStore(new_i64, bp.CreateStructGEP(obj_ty, val_addr, 1));
        bp.CreateBr(done_bb);

        IRBuilder<> bd(done_bb);
        bd.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_update_refs_heap(i64 old_i64, i64 new_i64) -> void
    // Phase B of reference updating: walks the allocation linked list,
    // scans all live objects' children, patches any reference matching
    // old_i64 to new_i64. Uses the same type dispatch as __ang_gc_scan.
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {i64_ty, i64_ty}, false);
        auto* fn = createRuntimeFunc("__ang_gc_update_refs_heap", fn_ty);
        m_fn_gc_update_refs_heap = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* loop_bb = BasicBlock::Create(m_ctx, "loop", fn);
        auto* check_live_bb = BasicBlock::Create(m_ctx, "check_live", fn);
        auto* scan_dispatch_bb = BasicBlock::Create(m_ctx, "scan_dispatch", fn);
        // Per-type scan blocks
        auto* scan_list_bb = BasicBlock::Create(m_ctx, "scan_list", fn);
        auto* scan_list_loop_bb = BasicBlock::Create(m_ctx, "scan_list_loop", fn);
        auto* scan_list_body_bb = BasicBlock::Create(m_ctx, "scan_list_body", fn);
        auto* scan_exception_bb = BasicBlock::Create(m_ctx, "scan_exception", fn);
        auto* scan_closure_bb = BasicBlock::Create(m_ctx, "scan_closure", fn);
        auto* scan_closure_loop_bb = BasicBlock::Create(m_ctx, "scan_closure_loop", fn);
        auto* scan_closure_body_bb = BasicBlock::Create(m_ctx, "scan_closure_body", fn);
        auto* scan_bound_bb = BasicBlock::Create(m_ctx, "scan_bound", fn);
        auto* scan_thread_bb = BasicBlock::Create(m_ctx, "scan_thread", fn);
        auto* scan_thread_loop_bb = BasicBlock::Create(m_ctx, "scan_thread_loop", fn);
        auto* scan_thread_body_bb = BasicBlock::Create(m_ctx, "scan_thread_body", fn);
        auto* scan_record_bb = BasicBlock::Create(m_ctx, "scan_record", fn);
        auto* scan_record_loop_bb = BasicBlock::Create(m_ctx, "scan_record_loop", fn);
        auto* scan_record_body_bb = BasicBlock::Create(m_ctx, "scan_record_body", fn);
        auto* advance_bb = BasicBlock::Create(m_ctx, "advance", fn);
        auto* done_bb = BasicBlock::Create(m_ctx, "done", fn);

        auto* update_fn = get_func("__ang_gc_update_ref_in_value");

        IRBuilder<> b(entry);
        auto* old_i64 = fn->arg_begin();
        auto* new_i64 = fn->arg_begin() + 1;
        auto* head = b.CreateLoad(i8_ptr, m_g_gc_alloc_list, "head");
        b.CreateBr(loop_bb);

        IRBuilder<> bl(loop_bb);
        auto* curr_phi = bl.CreatePHI(i8_ptr, 2, "curr");
        curr_phi->addIncoming(head, entry);
        auto* is_null = bl.CreateICmpEQ(curr_phi, ConstantPointerNull::get(i8_ptr));
        bl.CreateCondBr(is_null, done_bb, check_live_bb);

        IRBuilder<> bcl(check_live_bb);
        auto* meta = bcl.CreateLoad(i32_ty,
            bcl.CreateStructGEP(header_ty, curr_phi, 1), "meta");
        auto* color = bcl.CreateAnd(meta, ConstantInt::get(i32_ty, 0xFF), "color");
        auto* is_white = bcl.CreateICmpEQ(color, ConstantInt::get(i32_ty, COLOR_WHITE));
        auto* is_forwarded = bcl.CreateICmpEQ(color, ConstantInt::get(i32_ty, COLOR_FORWARDED));
        auto* skip = bcl.CreateOr(is_white, is_forwarded);
        bcl.CreateCondBr(skip, advance_bb, scan_dispatch_bb);

        IRBuilder<> bsd(scan_dispatch_bb);
        auto* obj_type = bsd.CreateLoad(i32_ty,
            bsd.CreateStructGEP(header_ty, curr_phi, 0), "obj_type");
        auto* sw = bsd.CreateSwitch(obj_type, advance_bb, 6);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_LIST), scan_list_bb);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_EXCEPTION), scan_exception_bb);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_CLOSURE), scan_closure_bb);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_BOUND_METHOD), scan_bound_bb);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_THREAD), scan_thread_bb);
        // RECORD, CLASS, INSTANCE, DATA_INSTANCE, ENUM_INSTANCE all share scan_record_bb
        sw->addCase(ConstantInt::get(i32_ty, OBJ_RECORD), scan_record_bb);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_CLASS), scan_record_bb);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_INSTANCE), scan_record_bb);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_DATA_INSTANCE), scan_record_bb);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_ENUM_INSTANCE), scan_record_bb);
        // STRING, MUTEX, NATIVE_INSTANCE: no children -> fall through to advance_bb

        // OBJ_LIST: loop over elems array
        {
            IRBuilder<> bsl(scan_list_bb);
            auto* count = bsl.CreateLoad(i64_ty,
                bsl.CreateStructGEP(m_list_type, curr_phi, 1), "count");
            auto* elems = bsl.CreateLoad(PointerType::get(m_ctx, 0),
                bsl.CreateStructGEP(m_list_type, curr_phi, 3), "elems");
            auto* has_elems = bsl.CreateICmpNE(elems, ConstantPointerNull::get(i8_ptr));
            bsl.CreateCondBr(has_elems, scan_list_loop_bb, advance_bb);

            IRBuilder<> bsll(scan_list_loop_bb);
            auto* i_phi = bsll.CreatePHI(i64_ty, 2, "i");
            i_phi->addIncoming(ConstantInt::get(i64_ty, 0), scan_list_bb);
            auto* cont = bsll.CreateICmpSLT(i_phi, count);
            bsll.CreateCondBr(cont, scan_list_body_bb, advance_bb);

            IRBuilder<> bslb(scan_list_body_bb);
            auto* elem_addr = bslb.CreateGEP(obj_ty, elems, {i_phi});
            bslb.CreateCall(update_fn, {elem_addr, old_i64, new_i64});
            auto* next_i = bslb.CreateAdd(i_phi, ConstantInt::get(i64_ty, 1));
            bslb.CreateBr(scan_list_loop_bb);
            i_phi->addIncoming(next_i, scan_list_body_bb);
        }

        // OBJ_EXCEPTION: patch msg (field 1)
        {
            IRBuilder<> bse(scan_exception_bb);
            auto* msg_addr = bse.CreateStructGEP(m_exception_type, curr_phi, 1);
            bse.CreateCall(update_fn, {msg_addr, old_i64, new_i64});
            bse.CreateBr(advance_bb);
        }

        // OBJ_CLOSURE: loop over env array
        {
            IRBuilder<> bsc(scan_closure_bb);
            auto* env = bsc.CreateLoad(PointerType::get(m_ctx, 0),
                bsc.CreateStructGEP(m_closure_type, curr_phi, 4), "env");
            auto* env_count = bsc.CreateLoad(i32_ty,
                bsc.CreateStructGEP(m_closure_type, curr_phi, 5), "env_count");
            auto* env_count_ext = bsc.CreateZExt(env_count, i64_ty, "env_count_ext");
            auto* has_env = bsc.CreateICmpNE(env, ConstantPointerNull::get(i8_ptr));
            bsc.CreateCondBr(has_env, scan_closure_loop_bb, advance_bb);

            IRBuilder<> bscl(scan_closure_loop_bb);
            auto* i_phi = bscl.CreatePHI(i64_ty, 2, "i");
            i_phi->addIncoming(ConstantInt::get(i64_ty, 0), scan_closure_bb);
            auto* cont = bscl.CreateICmpSLT(i_phi, env_count_ext);
            bscl.CreateCondBr(cont, scan_closure_body_bb, advance_bb);

            IRBuilder<> bscb(scan_closure_body_bb);
            auto* slot_addr = bscb.CreateGEP(obj_ty, env, {i_phi});
            bscb.CreateCall(update_fn, {slot_addr, old_i64, new_i64});
            auto* next_i = bscb.CreateAdd(i_phi, ConstantInt::get(i64_ty, 1));
            bscb.CreateBr(scan_closure_loop_bb);
            i_phi->addIncoming(next_i, scan_closure_body_bb);
        }

        // OBJ_BOUND_METHOD: patch receiver (field 1) and method (field 2)
        {
            IRBuilder<> bsb(scan_bound_bb);
            auto* recv_addr = bsb.CreateStructGEP(m_bound_method_type, curr_phi, 1);
            bsb.CreateCall(update_fn, {recv_addr, old_i64, new_i64});
            auto* meth_addr = bsb.CreateStructGEP(m_bound_method_type, curr_phi, 2);
            bsb.CreateCall(update_fn, {meth_addr, old_i64, new_i64});
            bsb.CreateBr(advance_bb);
        }

        // OBJ_THREAD: patch closure (field 2), loop over args (field 4)
        {
            IRBuilder<> bst(scan_thread_bb);
            auto* closure_addr = bst.CreateStructGEP(m_thread_type, curr_phi, 2);
            bst.CreateCall(update_fn, {closure_addr, old_i64, new_i64});
            auto* argc = bst.CreateLoad(i32_ty,
                bst.CreateStructGEP(m_thread_type, curr_phi, 3), "argc");
            auto* args = bst.CreateLoad(PointerType::get(m_ctx, 0),
                bst.CreateStructGEP(m_thread_type, curr_phi, 4), "args");
            auto* argc_ext = bst.CreateZExt(argc, i64_ty, "argc_ext");
            auto* has_args = bst.CreateAnd(
                bst.CreateICmpNE(args, ConstantPointerNull::get(i8_ptr)),
                bst.CreateICmpSGT(argc_ext, ConstantInt::get(i64_ty, 0)));
            bst.CreateCondBr(has_args, scan_thread_loop_bb, advance_bb);

            IRBuilder<> bstl(scan_thread_loop_bb);
            auto* i_phi = bstl.CreatePHI(i64_ty, 2, "i");
            i_phi->addIncoming(ConstantInt::get(i64_ty, 0), scan_thread_bb);
            auto* cont = bstl.CreateICmpSLT(i_phi, argc_ext);
            bstl.CreateCondBr(cont, scan_thread_body_bb, advance_bb);

            IRBuilder<> bstb(scan_thread_body_bb);
            auto* slot_addr = bstb.CreateGEP(obj_ty, args, {i_phi});
            bstb.CreateCall(update_fn, {slot_addr, old_i64, new_i64});
            auto* next_i = bstb.CreateAdd(i_phi, ConstantInt::get(i64_ty, 1));
            bstb.CreateBr(scan_thread_loop_bb);
            i_phi->addIncoming(next_i, scan_thread_body_bb);
        }

        // OBJ_RECORD/CLASS/INSTANCE/DATA_INSTANCE/ENUM_INSTANCE: loop over entries
        {
            IRBuilder<> bsr(scan_record_bb);
            auto* count = bsr.CreateLoad(i64_ty,
                bsr.CreateStructGEP(m_record_type, curr_phi, 1), "count");
            auto* entries = bsr.CreateLoad(PointerType::get(m_ctx, 0),
                bsr.CreateStructGEP(m_record_type, curr_phi, 3), "entries");
            auto* has_entries = bsr.CreateICmpNE(entries, ConstantPointerNull::get(i8_ptr));
            bsr.CreateCondBr(has_entries, scan_record_loop_bb, advance_bb);

            IRBuilder<> bsrl(scan_record_loop_bb);
            auto* i_phi = bsrl.CreatePHI(i64_ty, 2, "i");
            i_phi->addIncoming(ConstantInt::get(i64_ty, 0), scan_record_bb);
            auto* cont = bsrl.CreateICmpSLT(i_phi, count);
            bsrl.CreateCondBr(cont, scan_record_body_bb, advance_bb);

            IRBuilder<> bsrb(scan_record_body_bb);
            auto* entry_ptr = bsrb.CreateGEP(m_record_entry_type, entries, {i_phi});
            auto* val_addr = bsrb.CreateStructGEP(m_record_entry_type, entry_ptr, 1);
            bsrb.CreateCall(update_fn, {val_addr, old_i64, new_i64});
            auto* next_i = bsrb.CreateAdd(i_phi, ConstantInt::get(i64_ty, 1));
            bsrb.CreateBr(scan_record_loop_bb);
            i_phi->addIncoming(next_i, scan_record_body_bb);
        }

        // Advance to next object in allocation list
        IRBuilder<> ba(advance_bb);
        auto* next_ptr = ba.CreateLoad(i8_ptr,
            ba.CreateStructGEP(header_ty, curr_phi, 2), "next");
        curr_phi->addIncoming(next_ptr, advance_bb);
        ba.CreateBr(loop_bb);

        IRBuilder<> bd(done_bb);
        bd.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_update_refs(i8* old_ptr, i8* new_ptr) -> void
    // Scans all root frames and live (BLACK) objects, updating any
    // pointer that references old_ptr to point to new_ptr.
    //
    // In the biological analogy, this is the cellular machinery updating
    // all protein interaction maps after a residue has been relocated —
    // every reference must be corrected for the system to remain consistent.
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {i8_ptr, i8_ptr}, false);
        auto* fn = createRuntimeFunc("__ang_gc_update_refs", fn_ty);
        m_fn_gc_update_refs = FunctionCallee(fn);

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
        auto* old_ptr = fn->arg_begin();
        auto* new_ptr = fn->arg_begin() + 1;

        // Cast old/new to i64 for comparison
        auto* old_i64 = b.CreatePtrToInt(old_ptr, i64_ty, "old_i64");
        auto* new_i64 = b.CreatePtrToInt(new_ptr, i64_ty, "new_i64");

        // Walk all root frames and update root slots
        auto* threads = b.CreateLoad(i8_ptr, m_g_gc_threads, "threads");
        b.CreateBr(thread_loop_bb);

        IRBuilder<> btl(thread_loop_bb);
        auto* thread_phi = btl.CreatePHI(i8_ptr, 2, "thread");
        thread_phi->addIncoming(threads, entry);
        auto* thread_done = btl.CreateICmpEQ(thread_phi, ConstantPointerNull::get(i8_ptr));
        btl.CreateCondBr(thread_done, done_bb, frame_setup_bb);

        IRBuilder<> bfs(frame_setup_bb);
        auto* root_frames = bfs.CreateLoad(i8_ptr,
            bfs.CreateStructGEP(m_gc_thread_state_type, thread_phi, 2), "root_frames");
        bfs.CreateBr(frame_loop_bb);

        IRBuilder<> bfl(frame_loop_bb);
        auto* frame_phi = bfl.CreatePHI(i8_ptr, 2, "frame");
        frame_phi->addIncoming(root_frames, frame_setup_bb);
        auto* frame_done = bfl.CreateICmpEQ(frame_phi, ConstantPointerNull::get(i8_ptr));
        bfl.CreateCondBr(frame_done, next_thread_bb, slot_loop_bb);

        IRBuilder<> bsl(slot_loop_bb);
        auto* i_phi = bsl.CreatePHI(i64_ty, 2, "slot_i");
        i_phi->addIncoming(ConstantInt::get(i64_ty, 0), frame_loop_bb);
        auto* count = bsl.CreateLoad(i32_ty,
            bsl.CreateStructGEP(m_gc_root_frame_type, frame_phi, 1), "slot_count");
        auto* count_ext = bsl.CreateZExt(count, i64_ty, "count_ext");
        auto* has_more = bsl.CreateICmpSLT(i_phi, count_ext);
        bsl.CreateCondBr(has_more, slot_body_bb, next_frame_bb);

        IRBuilder<> bsb(slot_body_bb);
        // Each slot is a pointer to AngaraObject
        auto* slot_addr = bsb.CreateGEP(m_gc_root_frame_type, frame_phi,
            {ConstantInt::get(i32_ty, 0), ConstantInt::get(i32_ty, 2), i_phi});
        auto* obj_ptr = bsb.CreateLoad(i8_ptr, slot_addr, "obj_ptr");
        // obj_ptr points to an AngaraObject { i32 tag, i64 payload }
        // Load tag to check if it's TAG_OBJ (4)
        auto* tag = bsb.CreateLoad(i32_ty, obj_ptr, "tag");
        auto* is_obj = bsb.CreateICmpEQ(tag, ConstantInt::get(i32_ty, TAG_OBJ));
        auto* check_match_bb = BasicBlock::Create(m_ctx, "check_match", fn);
        auto* slot_next_bb = BasicBlock::Create(m_ctx, "slot_next", fn);
        bsb.CreateCondBr(is_obj, check_match_bb, slot_next_bb);

        IRBuilder<> bcm(check_match_bb);
        // Load payload (i64), compare with old_ptr
        auto* payload_addr = bcm.CreateStructGEP(obj_ty, obj_ptr, 1);
        auto* payload = bcm.CreateLoad(i64_ty, payload_addr, "payload");
        auto* matches = bcm.CreateICmpEQ(payload, old_i64);
        auto* do_update_bb = BasicBlock::Create(m_ctx, "do_update", fn);
        bcm.CreateCondBr(matches, do_update_bb, slot_next_bb);

        IRBuilder<> bdu(do_update_bb);
        // Update: store new_ptr as payload (cast to i64)
        bdu.CreateStore(new_i64, bdu.CreateStructGEP(obj_ty, obj_ptr, 1));
        bdu.CreateBr(slot_next_bb);

        IRBuilder<> bsn(slot_next_bb);
        auto* next_i = bsn.CreateAdd(i_phi, ConstantInt::get(i64_ty, 1));
        bsn.CreateBr(slot_loop_bb);
        i_phi->addIncoming(next_i, slot_next_bb);

        IRBuilder<> bnf(next_frame_bb);
        auto* prev_frame = bnf.CreateLoad(i8_ptr,
            bnf.CreateStructGEP(m_gc_root_frame_type, frame_phi, 0), "prev_frame");
        bnf.CreateBr(frame_loop_bb);
        frame_phi->addIncoming(prev_frame, next_frame_bb);

        IRBuilder<> bnt(next_thread_bb);
        auto* next_thread = bnt.CreateLoad(i8_ptr,
            bnt.CreateStructGEP(m_gc_thread_state_type, thread_phi, 1), "next_thread");
        bnt.CreateBr(thread_loop_bb);
        thread_phi->addIncoming(next_thread, next_thread_bb);

        IRBuilder<> bd(done_bb);
        // Phase B: scan all live heap objects for stale references
        bd.CreateCall(get_func("__ang_gc_update_refs_heap"), {old_i64, new_i64});
        bd.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_chaperone_step() -> void
    // One step of the simulated annealing optimization.
    //
    // In the biological analogy, this is the chaperone molecule evaluating
    // the free-energy landscape and proposing a small conformational change
    // (object relocation) to lower the system's total free energy.
    //
    // Algorithm:
    // 1. Find the highest-energy arena
    // 2. Pick a random live object from it
    // 3. Try to move it to a lower-energy arena
    // 4. Accept if delta_E < 0, or with probability exp(-delta_E / T)
    // 5. Cool: T *= 0.9999, reheat every 10000 steps
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {}, false);
        auto* fn = createRuntimeFunc("__ang_gc_chaperone_step", fn_ty);
        m_fn_gc_chaperone_step = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* scan_bb = BasicBlock::Create(m_ctx, "scan_arenas", fn);
        auto* scan_body_bb = BasicBlock::Create(m_ctx, "scan_body", fn);
        auto* check_best_bb = BasicBlock::Create(m_ctx, "check_best", fn);
        auto* find_src_bb = BasicBlock::Create(m_ctx, "find_src", fn);
        auto* walk_src_bb = BasicBlock::Create(m_ctx, "walk_src", fn);
        auto* walk_body_bb = BasicBlock::Create(m_ctx, "walk_body", fn);
        auto* found_live_bb = BasicBlock::Create(m_ctx, "found_live", fn);
        auto* not_pinned_bb = BasicBlock::Create(m_ctx, "not_pinned", fn);
        auto* do_move_bb = BasicBlock::Create(m_ctx, "do_move", fn);
        auto* cool_bb = BasicBlock::Create(m_ctx, "cool", fn);
        auto* done_bb = BasicBlock::Create(m_ctx, "done", fn);

        IRBuilder<> b(entry);

        // If GC is running, skip this step
        auto* running = b.CreateLoad(i1_ty, m_g_gc_running, "running");
        b.CreateCondBr(running, done_bb, scan_bb);

        // --- Phase 1: Find highest-energy arena ---
        IRBuilder<> bs(scan_bb);
        auto* arena_count = bs.CreateLoad(i64_ty, m_g_gc_arena_count, "arena_count");
        auto* has_arenas = bs.CreateICmpSGT(arena_count, ConstantInt::get(i64_ty, 1));
        bs.CreateCondBr(has_arenas, scan_body_bb, done_bb);
        // Need at least 2 arenas to move between

        IRBuilder<> bsb(scan_body_bb);
        auto* i_phi = bsb.CreatePHI(i64_ty, 2, "i");
        i_phi->addIncoming(ConstantInt::get(i64_ty, 0), scan_bb);
        auto* best_energy_phi = bsb.CreatePHI(f64_ty, 2, "best_e");
        best_energy_phi->addIncoming(ConstantFP::get(f64_ty, -1.0), scan_bb);
        auto* best_idx_phi = bsb.CreatePHI(i64_ty, 2, "best_idx");
        best_idx_phi->addIncoming(ConstantInt::get(i64_ty, 0), scan_bb);

        auto* at_end = bsb.CreateICmpSLT(i_phi, arena_count);
        bsb.CreateCondBr(at_end, check_best_bb, find_src_bb);

        IRBuilder<> bcb(check_best_bb);
        auto* arena_ptr_addr = bcb.CreateInBoundsGEP(
            ArrayType::get(i8_ptr, 256), m_g_gc_arenas,
            {ConstantInt::get(i64_ty, 0), i_phi});
        auto* arena = bcb.CreateLoad(i8_ptr, arena_ptr_addr, "arena");
        auto* is_null = bcb.CreateICmpEQ(arena, ConstantPointerNull::get(i8_ptr));
        auto* skip_bb = BasicBlock::Create(m_ctx, "skip_arena", fn);
        auto* compute_bb = BasicBlock::Create(m_ctx, "compute", fn);
        bcb.CreateCondBr(is_null, skip_bb, compute_bb);

        IRBuilder<> bsk(skip_bb);
        auto* next_i_skip = bsk.CreateAdd(i_phi, ConstantInt::get(i64_ty, 1));
        bsk.CreateBr(scan_body_bb);
        i_phi->addIncoming(next_i_skip, skip_bb);
        best_energy_phi->addIncoming(best_energy_phi, skip_bb);
        best_idx_phi->addIncoming(best_idx_phi, skip_bb);

        IRBuilder<> bcomp(compute_bb);
        auto* energy = bcomp.CreateCall(get_func("__ang_gc_compute_energy"), {arena}, "energy");
        auto* is_better = bcomp.CreateFCmpOGT(energy, best_energy_phi);
        auto* new_best_e = bcomp.CreateSelect(is_better, energy, best_energy_phi);
        auto* new_best_idx = bcomp.CreateSelect(is_better, i_phi, best_idx_phi);
        auto* next_i = bcomp.CreateAdd(i_phi, ConstantInt::get(i64_ty, 1));
        bcomp.CreateBr(scan_body_bb);
        i_phi->addIncoming(next_i, compute_bb);
        best_energy_phi->addIncoming(new_best_e, compute_bb);
        best_idx_phi->addIncoming(new_best_idx, compute_bb);

        // --- Phase 2: Find a live object in the worst arena ---
        IRBuilder<> bfs(find_src_bb);
        auto* best_arena_addr = bfs.CreateInBoundsGEP(
            ArrayType::get(i8_ptr, 256), m_g_gc_arenas,
            {ConstantInt::get(i64_ty, 0), best_idx_phi});
        auto* src_arena = bfs.CreateLoad(i8_ptr, best_arena_addr, "src_arena");
        // Arena may have been recycled by a concurrent GC collection
        auto* src_is_null = bfs.CreateICmpEQ(src_arena, ConstantPointerNull::get(i8_ptr));
        auto* load_src_bb = BasicBlock::Create(m_ctx, "load_src", fn);
        bfs.CreateCondBr(src_is_null, done_bb, load_src_bb);

        IRBuilder<> bls(load_src_bb);
        auto* src_base = bls.CreateLoad(i8_ptr,
            bls.CreateStructGEP(arena_ty, src_arena, 0), "src_base");
        auto* src_bump = bls.CreateLoad(i8_ptr,
            bls.CreateStructGEP(arena_ty, src_arena, 1), "src_bump");
        bls.CreateBr(walk_src_bb);

        IRBuilder<> bws(walk_src_bb);
        auto* cur_phi = bws.CreatePHI(i8_ptr, 2, "cur");
        cur_phi->addIncoming(src_base, load_src_bb);
        auto* at_end2 = bws.CreateICmpUGE(cur_phi, src_bump);
        bws.CreateCondBr(at_end2, done_bb, walk_body_bb);  // no live obj found, done

        IRBuilder<> bwb(walk_body_bb);
        auto* meta = bwb.CreateLoad(i32_ty,
            bwb.CreateStructGEP(header_ty, cur_phi, 1), "meta");
        auto* color = bwb.CreateAnd(meta, ConstantInt::get(i32_ty, 0xFF), "color");
        auto* is_white = bwb.CreateICmpEQ(color, ConstantInt::get(i32_ty, COLOR_WHITE));
        auto* is_forwarded = bwb.CreateICmpEQ(color, ConstantInt::get(i32_ty, COLOR_FORWARDED));
        auto* is_dead = bwb.CreateOr(is_white, is_forwarded);
        auto* stride = bwb.CreateCall(get_func("__ang_gc_obj_size"), {cur_phi}, "stride");
        auto* next_cur = bwb.CreateGEP(i8_ty, cur_phi, {stride});
        bwb.CreateCondBr(is_dead, walk_src_bb, found_live_bb);
        cur_phi->addIncoming(next_cur, walk_body_bb);

        // --- Phase 3: Move the live object ---
        IRBuilder<> bfl(found_live_bb);
        // Pin the object to prevent GC from collecting it during move
        auto* pinned = bfl.CreateAnd(meta, ConstantInt::get(i32_ty, 1 << PINNED_SHIFT));
        auto* already_pinned = bfl.CreateICmpNE(pinned, ConstantInt::get(i32_ty, 0));
        bfl.CreateCondBr(already_pinned, walk_src_bb, not_pinned_bb);
        cur_phi->addIncoming(next_cur, found_live_bb);

        IRBuilder<> bnp(not_pinned_bb);
        // Allocate destination in a different arena using bump allocator
        auto* obj_type_val = bnp.CreateLoad(i32_ty,
            bnp.CreateStructGEP(header_ty, cur_phi, 0), "obj_type_val");
        auto* obj_alloc_size = bnp.CreateCall(get_func("__ang_gc_obj_size"), {cur_phi}, "obj_alloc_size");
        auto* alloc_fn = get_func("__ang_gc_alloc");
        auto* dest = bnp.CreateCall(alloc_fn, {obj_alloc_size, obj_type_val}, "dest");
        bnp.CreateBr(do_move_bb);

        IRBuilder<> bdm(do_move_bb);
        // Relocate: memcpy + set forwarding pointer
        auto* relocated = bdm.CreateCall(get_func("__ang_gc_relocate"), {cur_phi, dest}, "relocated");
        // Update all references to point to new location
        bdm.CreateCall(get_func("__ang_gc_update_refs"), {cur_phi, relocated});
        bdm.CreateBr(cool_bb);

        // --- Phase 4: Simulated annealing temperature update ---
        IRBuilder<> bc(cool_bb);
        auto* temp = bc.CreateLoad(f64_ty, m_g_gc_temperature, "temp");
        // Cool: T *= 0.9999
        auto* cooled = bc.CreateFMul(temp, ConstantFP::get(f64_ty, 0.9999), "cooled");
        // Increment step count
        auto* step = bc.CreateLoad(i64_ty, m_g_gc_step_count, "step");
        auto* new_step = bc.CreateAdd(step, ConstantInt::get(i64_ty, 1));
        bc.CreateStore(new_step, m_g_gc_step_count);
        // Reheat every 10000 steps: T = 1.0
        auto* reheat_cycle = bc.CreateSRem(new_step, ConstantInt::get(i64_ty, 10000));
        auto* should_reheat = bc.CreateICmpEQ(reheat_cycle, ConstantInt::get(i64_ty, 0));
        auto* final_temp = bc.CreateSelect(should_reheat,
            ConstantFP::get(f64_ty, 1.0), cooled);
        // Clamp minimum temperature
        auto* above_min = bc.CreateFCmpOGT(final_temp, ConstantFP::get(f64_ty, 0.01));
        auto* clamped_temp = bc.CreateSelect(above_min,
            final_temp, ConstantFP::get(f64_ty, 0.01));
        bc.CreateStore(clamped_temp, m_g_gc_temperature);
        bc.CreateBr(done_bb);

        IRBuilder<> bd(done_bb);
        bd.CreateRetVoid();
    }

    // ===================================================================
    // __ang_gc_chaperone_main(i8* arg) -> i8*
    // Background thread entry point for the chaperone.
    //
    // In the biological analogy, this is the chaperone molecule's lifecycle:
    // it continuously monitors the protein population (heap), proposing
    // small conformational changes (relocations) to guide the system toward
    // its native free-energy minimum (compact, cache-friendly layout).
    //
    // Loop: step() → usleep(1000) → check active flag
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(i8_ptr, {i8_ptr}, false);
        auto* fn = createRuntimeFunc("__ang_gc_chaperone_main", fn_ty);
        m_fn_gc_chaperone_main = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* loop_bb = BasicBlock::Create(m_ctx, "loop", fn);
        auto* step_bb = BasicBlock::Create(m_ctx, "step", fn);
        auto* sleep_bb = BasicBlock::Create(m_ctx, "sleep", fn);
        auto* exit_bb = BasicBlock::Create(m_ctx, "exit", fn);

        IRBuilder<> b(entry);
        // Register as a GC thread so we participate in STW pauses
        // But we don't allocate a new GcThreadState — we share the mutator's.
        // Actually, for simplicity, the chaperone doesn't register as a GC thread.
        // It just checks gc_running before each step.
        b.CreateBr(loop_bb);

        IRBuilder<> bl(loop_bb);
        auto* active = bl.CreateLoad(i1_ty, m_g_gc_chaperone_active, "active");
        bl.CreateCondBr(active, step_bb, exit_bb);

        IRBuilder<> bst(step_bb);
        bst.CreateCall(get_func("__ang_gc_chaperone_step"), {});
        bst.CreateBr(sleep_bb);

        IRBuilder<> bsl(sleep_bb);
        auto* usleep_fn = m_module.getFunction("usleep");
        bsl.CreateCall(usleep_fn, {ConstantInt::get(i32_ty, 1000)}); // 1ms
        bsl.CreateBr(loop_bb);

        IRBuilder<> be(exit_bb);
        be.CreateRet(ConstantPointerNull::get(i8_ptr));
    }

    // ===================================================================
    // __ang_gc_chaperone_spawn() -> i8*
    // Lazily spawns the chaperone background thread on first arena alloc.
    //
    // In the biological analogy, this is the cell producing chaperone
    // molecules on demand when protein folding demand increases.
    // ===================================================================
    {
        auto* fn_ty = FunctionType::get(i8_ptr, {}, false);
        auto* fn = createRuntimeFunc("__ang_gc_chaperone_spawn", fn_ty);
        m_fn_gc_chaperone_spawn = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* already_active_bb = BasicBlock::Create(m_ctx, "already_active", fn);
        auto* spawn_bb = BasicBlock::Create(m_ctx, "spawn", fn);
        auto* done_bb = BasicBlock::Create(m_ctx, "done", fn);

        IRBuilder<> b(entry);
        auto* active = b.CreateLoad(i1_ty, m_g_gc_chaperone_active, "active");
        b.CreateCondBr(active, already_active_bb, spawn_bb);

        IRBuilder<> ba(already_active_bb);
        auto* thread = ba.CreateLoad(i8_ptr, m_g_gc_chaperone_thread, "thread");
        ba.CreateBr(done_bb);

        IRBuilder<> bsp(spawn_bb);
        // Chaperone compaction runs synchronously during GC collection,
        // not as a background thread — avoids concurrency hazards with
        // the allocation linked list's forward/next pointer overlap.
        // Set active flag so stats/reporting can detect it.
        bsp.CreateStore(ConstantInt::getTrue(m_ctx), m_g_gc_chaperone_active);
        bsp.CreateBr(done_bb);

        IRBuilder<> bd(done_bb);
        auto* result = bd.CreatePHI(i8_ptr, 2, "result");
        result->addIncoming(thread, already_active_bb);
        result->addIncoming(ConstantPointerNull::get(i8_ptr), spawn_bb);
        bd.CreateRet(result);
    }
}

void ChaperoneGC::generateFreestandingStubs() {
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

    // gc_alloc -> malloc
    {
        auto callee = m_module.getOrInsertFunction("__ang_gc_alloc",
            FunctionType::get(i8_ptr, {i64_ty, i32_ty}, false));
        auto* fn = cast<Function>(callee.getCallee());
        fn->setLinkage(Function::InternalLinkage);
        fn->setDSOLocal(true);
        auto* e = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(e);
        b.CreateRet(b.CreateCall(m_module.getFunction("malloc"), {fn->arg_begin()}));
        m_fn_gc_alloc = FunctionCallee(fn);
    }

    stub_void("__ang_gc_alloc_slow", FunctionType::get(i8_ptr, {i64_ty, i32_ty}, false));
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

    // Read barrier: identity
    {
        auto callee = m_module.getOrInsertFunction("__ang_gc_read_barrier",
            FunctionType::get(i8_ptr, {i8_ptr}, false));
        auto* fn = cast<Function>(callee.getCallee());
        fn->setLinkage(Function::InternalLinkage);
        fn->setDSOLocal(true);
        auto* e = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<>(e).CreateRet(fn->arg_begin());
        m_fn_gc_read_barrier = FunctionCallee(fn);
    }

    // Chaperone stubs
    stub_void("__ang_gc_chaperone_step", FunctionType::get(void_ty, {}, false));
    stub_void("__ang_gc_update_refs", FunctionType::get(void_ty, {i8_ptr, i8_ptr}, false));

    {
        auto callee = m_module.getOrInsertFunction("__ang_gc_compute_energy",
            FunctionType::get(Type::getDoubleTy(m_ctx), {i8_ptr}, false));
        auto* fn = cast<Function>(callee.getCallee());
        fn->setLinkage(Function::InternalLinkage);
        fn->setDSOLocal(true);
        auto* e = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<>(e).CreateRet(ConstantFP::get(Type::getDoubleTy(m_ctx), 0.0));
        m_fn_gc_compute_energy = FunctionCallee(fn);
    }
    {
        auto callee = m_module.getOrInsertFunction("__ang_gc_relocate",
            FunctionType::get(i8_ptr, {i8_ptr, i8_ptr}, false));
        auto* fn = cast<Function>(callee.getCallee());
        fn->setLinkage(Function::InternalLinkage);
        fn->setDSOLocal(true);
        auto* e = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<>(e).CreateRet(fn->arg_begin()); // return old ptr (identity)
        m_fn_gc_relocate = FunctionCallee(fn);
    }
    {
        auto callee = m_module.getOrInsertFunction("__ang_gc_chaperone_spawn",
            FunctionType::get(i8_ptr, {}, false));
        auto* fn = cast<Function>(callee.getCallee());
        fn->setLinkage(Function::InternalLinkage);
        fn->setDSOLocal(true);
        auto* e = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<>(e).CreateRet(ConstantPointerNull::get(i8_ptr));
        m_fn_gc_chaperone_spawn = FunctionCallee(fn);
    }
    {
        auto callee = m_module.getOrInsertFunction("__ang_gc_chaperone_main",
            FunctionType::get(i8_ptr, {i8_ptr}, false));
        auto* fn = cast<Function>(callee.getCallee());
        fn->setLinkage(Function::InternalLinkage);
        fn->setDSOLocal(true);
        auto* e = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<>(e).CreateRet(ConstantPointerNull::get(i8_ptr));
        m_fn_gc_chaperone_main = FunctionCallee(fn);
    }
}

} // namespace angara
