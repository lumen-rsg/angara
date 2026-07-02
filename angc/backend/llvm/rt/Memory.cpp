#include "RuntimeBuilder.h"

using namespace llvm;

namespace angara {

// ============================================================================
// generateMemoryManagement — the v5 runtime memory layer.
//
// All allocations route through a single Allocator vtable:
//   %Allocator = { ptr alloc(i64), ptr realloc(ptr,i64,i64), ptr free(ptr,i64) }
//
// The default allocator wraps libc malloc/realloc/free. In freestanding mode
// (or for scoped arena allocation) the user swaps the global allocator
// pointer via __ang_allocator_set.
//
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

    auto* malloc_fn  = m_module.getFunction("malloc");
    auto* realloc_fn = m_module.getFunction("realloc");
    auto* free_fn    = m_module.getFunction("free");

    // ========================================================================
    // Allocator type + default implementation + global pointer
    // ========================================================================

    // Allocator vtable: { ptr alloc(i64)->ptr, ptr realloc(ptr,i64,i64)->ptr, ptr free(ptr,i64)->void }
    auto* alloc_fn_ty   = FunctionType::get(i8_ptr, {i64_ty}, false);
    auto* realloc_fn_ty = FunctionType::get(i8_ptr, {i8_ptr, i64_ty, i64_ty}, false);
    auto* free_fn_ty    = FunctionType::get(void_ty, {i8_ptr, i64_ty}, false);

    m_allocator_type = StructType::create(m_ctx, {
        PointerType::get(m_ctx, 0),  // alloc
        PointerType::get(m_ctx, 0),  // realloc
        PointerType::get(m_ctx, 0),  // free
    }, "AngaraAllocator");

    // --- Default allocator implementation (wraps libc) ---
    auto* alloc_impl = createRuntimeFunc("__ang_alloc_impl", alloc_fn_ty);
    {
        auto* entry = BasicBlock::Create(m_ctx, "entry", alloc_impl);
        IRBuilder<> b(entry);
        b.CreateRet(b.CreateCall(malloc_fn, {alloc_impl->arg_begin()}, "mem"));
    }
    auto* realloc_impl = createRuntimeFunc("__ang_realloc_impl", realloc_fn_ty);
    {
        auto* entry = BasicBlock::Create(m_ctx, "entry", realloc_impl);
        IRBuilder<> b(entry);
        // realloc(p, new_size) — old_size ignored by libc
        b.CreateRet(b.CreateCall(realloc_fn,
            {realloc_impl->arg_begin(), realloc_impl->arg_begin() + 2}, "grown"));
    }
    auto* free_impl = createRuntimeFunc("__ang_free_impl", free_fn_ty);
    {
        auto* entry = BasicBlock::Create(m_ctx, "entry", free_impl);
        IRBuilder<> b(entry);
        b.CreateCall(free_fn, {free_impl->arg_begin()});
        b.CreateRetVoid();
    }

    // Default allocator constant
    auto* default_allocator = new GlobalVariable(
        m_module, m_allocator_type, true, GlobalValue::PrivateLinkage,
        ConstantStruct::get(m_allocator_type,
            {alloc_impl, realloc_impl, free_impl}),
        "__ang_default_allocator");

    // Current allocator pointer (mutable — users can swap it)
    m_g_allocator = new GlobalVariable(
        m_module, PointerType::get(m_ctx, 0), false, GlobalValue::InternalLinkage,
        default_allocator, "__ang_allocator");

    // --- __ang_allocator_get() -> ptr : return current allocator ---
    {
        auto* fn = createRuntimeFunc("__ang_allocator_get",
            FunctionType::get(i8_ptr, {}, false));
        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        b.CreateRet(b.CreateLoad(i8_ptr, m_g_allocator, "alloc_ptr"));
    }

    // --- __ang_allocator_set(ptr alloc) -> void : swap allocator ---
    {
        auto* fn = createRuntimeFunc("__ang_allocator_set",
            FunctionType::get(void_ty, {i8_ptr}, false));
        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        b.CreateStore(fn->arg_begin(), m_g_allocator);
        b.CreateRetVoid();
    }

    // ========================================================================
    // __ang_gc_alloc(i64 size, i32 type) -> i8*
    // Routes through the Allocator vtable: load alloc fn, call it, init header.
    // ========================================================================
    {
        auto* fn = createRuntimeFunc("__ang_gc_alloc",
            FunctionType::get(i8_ptr, {i64_ty, i32_ty}, false));
        m_fn_gc_alloc = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* size_arg = fn->arg_begin();
        auto* type_arg = fn->arg_begin() + 1;

        // Load allocator → load alloc fn ptr → call
        auto* alloc_ptr = b.CreateLoad(PointerType::get(m_ctx, 0), m_g_allocator, "allocator");
        auto* alloc_fn_slot = b.CreateStructGEP(m_allocator_type, alloc_ptr, 0, "alloc_slot");
        auto* alloc_fn_val = b.CreateLoad(PointerType::get(m_ctx, 0), alloc_fn_slot, "alloc_fn");
        auto* mem = b.CreateCall(alloc_fn_ty, alloc_fn_val, {size_arg}, "mem");

        // Init ObjHeader: type, meta (is_unique), next=null
        b.CreateStore(type_arg, b.CreateStructGEP(m_obj_header_type, mem, 0));
        b.CreateStore(m_gc_initial_meta, b.CreateStructGEP(m_obj_header_type, mem, 1));
        b.CreateStore(ConstantPointerNull::get(i8_ptr),
                       b.CreateStructGEP(m_obj_header_type, mem, 2));
        b.CreateRet(mem);
    }

    // --- __ang_gc_free(i8* obj, i64 size) -> void : routes through Allocator ---
    // Used by `drop` (Stage 2) and __ang_gc_finalize.
    {
        auto* fn = createRuntimeFunc("__ang_gc_free",
            FunctionType::get(void_ty, {i8_ptr, i64_ty}, false));
        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* obj_arg  = fn->arg_begin();
        auto* size_arg = fn->arg_begin() + 1;
        auto* alloc_ptr = b.CreateLoad(PointerType::get(m_ctx, 0), m_g_allocator, "allocator");
        auto* free_fn_slot = b.CreateStructGEP(m_allocator_type, alloc_ptr, 2, "free_slot");
        auto* free_fn_val = b.CreateLoad(PointerType::get(m_ctx, 0), free_fn_slot, "free_fn");
        b.CreateCall(free_fn_ty, free_fn_val, {obj_arg, size_arg});
        b.CreateRetVoid();
    }

    // ========================================================================
    // No-op stubs (no collection, no safepoints, no barriers)
    // ========================================================================

    auto stub_void = [&](const std::string& name, FunctionCallee& callee_out) {
        auto* fn = createRuntimeFunc(name, FunctionType::get(void_ty, {}, false));
        callee_out = FunctionCallee(fn);
        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<>(entry).CreateRetVoid();
    };
    auto stub_void_ptr = [&](const std::string& name, FunctionCallee& callee_out) {
        auto* fn = createRuntimeFunc(name, FunctionType::get(void_ty, {i8_ptr}, false));
        callee_out = FunctionCallee(fn);
        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<>(entry).CreateRetVoid();
    };

    // --- __ang_gc_read_barrier(i8*) -> i8* : identity ---
    {
        auto* fn = createRuntimeFunc("__ang_gc_read_barrier",
            FunctionType::get(i8_ptr, {i8_ptr}, false));
        m_fn_gc_read_barrier = FunctionCallee(fn);
        IRBuilder<>(BasicBlock::Create(m_ctx, "entry", fn)).CreateRet(fn->arg_begin());
    }

    // --- Thread register/unregister: set/clear TLS ---
    {
        auto* fn = createRuntimeFunc("__ang_gc_thread_register",
            FunctionType::get(void_ty, {i8_ptr}, false));
        m_fn_gc_thread_register = FunctionCallee(fn);
        IRBuilder<> b(BasicBlock::Create(m_ctx, "entry", fn));
        b.CreateStore(fn->arg_begin(), m_g_thread_state_tls);
        b.CreateRetVoid();
    }
    {
        auto* fn = createRuntimeFunc("__ang_gc_thread_unregister",
            FunctionType::get(void_ty, {i8_ptr}, false));
        m_fn_gc_thread_unregister = FunctionCallee(fn);
        IRBuilder<> b(BasicBlock::Create(m_ctx, "entry", fn));
        b.CreateStore(ConstantPointerNull::get(i8_ptr), m_g_thread_state_tls);
        b.CreateRetVoid();
    }

    // --- clear_unique(AngaraObject) : no-op ---
    {
        auto* fn = createRuntimeFunc("__ang_gc_clear_unique",
            FunctionType::get(void_ty, {m_angara_obj_type}, false));
        m_fn_gc_clear_unique = FunctionCallee(fn);
        IRBuilder<>(BasicBlock::Create(m_ctx, "entry", fn)).CreateRetVoid();
    }

    // --- No-op stubs ---
    stub_void("__ang_gc_collect",       m_fn_gc_collect);
    stub_void("__ang_gc_safepoint",     m_fn_gc_safepoint);
    stub_void("__ang_gc_pop_frame",     m_fn_gc_pop_frame);
    stub_void("__ang_gc_print_stats",   m_fn_gc_print_stats);
    stub_void_ptr("__ang_gc_push_frame", m_fn_gc_push_frame);

    // pin/unpin take AngaraObject (not i8*)
    {
        auto* fn = createRuntimeFunc("__ang_gc_pin",
            FunctionType::get(void_ty, {m_angara_obj_type}, false));
        m_fn_gc_pin = FunctionCallee(fn);
        IRBuilder<>(BasicBlock::Create(m_ctx, "entry", fn)).CreateRetVoid();
    }
    {
        auto* fn = createRuntimeFunc("__ang_gc_unpin",
            FunctionType::get(void_ty, {m_angara_obj_type}, false));
        m_fn_gc_unpin = FunctionCallee(fn);
        IRBuilder<>(BasicBlock::Create(m_ctx, "entry", fn)).CreateRetVoid();
    }

    // --- finalize: no-op for now (Stage 2 will free internal buffers via __ang_gc_free) ---
    stub_void_ptr("__ang_gc_finalize",  m_fn_gc_finalize);

    // --- obj_size: return 0 (unused without compaction) ---
    {
        auto* fn = createRuntimeFunc("__ang_gc_obj_size",
            FunctionType::get(i64_ty, {i8_ptr}, false));
        m_fn_gc_obj_size = FunctionCallee(fn);
        IRBuilder<>(BasicBlock::Create(m_ctx, "entry", fn))
            .CreateRet(ConstantInt::get(i64_ty, 0));
    }
}

} // namespace angara
