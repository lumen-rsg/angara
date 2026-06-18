#include "RuntimeBuilder.h"

using namespace llvm;

namespace angara {

// ============================================================================
// generateMemoryManagement — the v5 "no-GC" runtime.
//
// Every allocation goes through __ang_gc_alloc (plain malloc + ObjHeader init).
// Collection, safepoints, root frames, pinning, barriers are all no-ops.
// Thread register/unregister just set/clear the TLS pointer.
// This replaces the entire ChaperoneGC + MarkSweepGC runtime (~5000 lines).
// ============================================================================

void RuntimeBuilder::generateMemoryManagement() {
    auto* void_ty = Type::getVoidTy(m_ctx);
    auto* i32_ty  = Type::getInt32Ty(m_ctx);
    auto* i64_ty  = Type::getInt64Ty(m_ctx);
    auto* i8_ty   = Type::getInt8Ty(m_ctx);
    auto* i8_ptr  = PointerType::get(m_ctx, 0);

    // --- Thread-state TLS (used by thread setup codegen) ---
    m_g_thread_state_tls = new GlobalVariable(
        m_module, i8_ptr, false, GlobalValue::CommonLinkage,
        ConstantPointerNull::get(i8_ptr), "__ang_gc_thread_state");
    m_g_thread_state_tls->setThreadLocal(true);

    auto* malloc_fn = m_module.getFunction("malloc");

    // Helper: emit a void() no-op and store the callee.
    auto stub_void = [&](const std::string& name, FunctionCallee& callee_out) {
        auto* fn = createRuntimeFunc(name, FunctionType::get(void_ty, {}, false));
        callee_out = FunctionCallee(fn);
        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<>(entry).CreateRetVoid();
    };

    // Helper: emit a void(i8*) no-op.
    auto stub_void_ptr = [&](const std::string& name, FunctionCallee& callee_out) {
        auto* fn = createRuntimeFunc(name, FunctionType::get(void_ty, {i8_ptr}, false));
        callee_out = FunctionCallee(fn);
        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<>(entry).CreateRetVoid();
    };

    // --- __ang_gc_alloc(i64 size, i32 type) -> i8* : malloc + ObjHeader init ---
    {
        auto* fn = createRuntimeFunc("__ang_gc_alloc",
            FunctionType::get(i8_ptr, {i64_ty, i32_ty}, false));
        m_fn_gc_alloc = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* size_arg = fn->arg_begin();
        auto* type_arg = fn->arg_begin() + 1;

        auto* mem = b.CreateCall(malloc_fn, {size_arg}, "mem");
        // Init ObjHeader: type, meta (is_unique), next=null
        b.CreateStore(type_arg, b.CreateStructGEP(m_obj_header_type, mem, 0));
        b.CreateStore(m_gc_initial_meta, b.CreateStructGEP(m_obj_header_type, mem, 1));
        b.CreateStore(ConstantPointerNull::get(i8_ptr),
                       b.CreateStructGEP(m_obj_header_type, mem, 2));
        b.CreateRet(mem);
    }

    // --- __ang_gc_read_barrier(i8*) -> i8* : identity (no moving GC) ---
    {
        auto* fn = createRuntimeFunc("__ang_gc_read_barrier",
            FunctionType::get(i8_ptr, {i8_ptr}, false));
        m_fn_gc_read_barrier = FunctionCallee(fn);
        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<>(entry).CreateRet(fn->arg_begin());
    }

    // --- __ang_gc_thread_register(i8* state) : set TLS ---
    {
        auto* fn = createRuntimeFunc("__ang_gc_thread_register",
            FunctionType::get(void_ty, {i8_ptr}, false));
        m_fn_gc_thread_register = FunctionCallee(fn);
        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        b.CreateStore(fn->arg_begin(), m_g_thread_state_tls);
        b.CreateRetVoid();
    }

    // --- __ang_gc_thread_unregister(i8* state) : clear TLS ---
    {
        auto* fn = createRuntimeFunc("__ang_gc_thread_unregister",
            FunctionType::get(void_ty, {i8_ptr}, false));
        m_fn_gc_thread_unregister = FunctionCallee(fn);
        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        b.CreateStore(ConstantPointerNull::get(i8_ptr), m_g_thread_state_tls);
        b.CreateRetVoid();
    }

    // --- __ang_gc_clear_unique(AngaraObject) : no-op ---
    {
        auto* fn = createRuntimeFunc("__ang_gc_clear_unique",
            FunctionType::get(void_ty, {m_angara_obj_type}, false));
        m_fn_gc_clear_unique = FunctionCallee(fn);
        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<>(entry).CreateRetVoid();
    }

    // --- No-op stubs ---
    stub_void("__ang_gc_collect",           m_fn_gc_collect);
    stub_void("__ang_gc_safepoint",         m_fn_gc_safepoint);
    stub_void("__ang_gc_pop_frame",         m_fn_gc_pop_frame);
    stub_void("__ang_gc_print_stats",       m_fn_gc_print_stats);

    stub_void_ptr("__ang_gc_push_frame",    m_fn_gc_push_frame);

    // pin/unpin take AngaraObject (not i8*) — matching the codegen call sites.
    {
        auto* fn = createRuntimeFunc("__ang_gc_pin",
            FunctionType::get(void_ty, {m_angara_obj_type}, false));
        m_fn_gc_pin = FunctionCallee(fn);
        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<>(entry).CreateRetVoid();
    }
    {
        auto* fn = createRuntimeFunc("__ang_gc_unpin",
            FunctionType::get(void_ty, {m_angara_obj_type}, false));
        m_fn_gc_unpin = FunctionCallee(fn);
        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<>(entry).CreateRetVoid();
    }

    // --- __ang_gc_finalize(i8*) -> void : no-op for now (will free buffers in Stage 2) ---
    stub_void_ptr("__ang_gc_finalize",      m_fn_gc_finalize);

    // --- __ang_gc_obj_size(i8*) -> i64 : return 0 (unused without compaction) ---
    {
        auto* fn = createRuntimeFunc("__ang_gc_obj_size",
            FunctionType::get(i64_ty, {i8_ptr}, false));
        m_fn_gc_obj_size = FunctionCallee(fn);
        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<>(entry).CreateRet(ConstantInt::get(i64_ty, 0));
    }
}

} // namespace angara
