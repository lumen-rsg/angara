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
// This replaces the entire prior GC-based runtime (~5000 lines).
// ============================================================================

void RuntimeBuilder::generateMemoryManagement() {
    auto* void_ty = Type::getVoidTy(m_ctx);
    auto* i32_ty  = Type::getInt32Ty(m_ctx);
    auto* i64_ty  = Type::getInt64Ty(m_ctx);
    auto* i8_ty   = Type::getInt8Ty(m_ctx);
    auto* i8_ptr  = PointerType::get(m_ctx, 0);

    // --- Thread-state TLS (used by thread setup codegen) ---
    // InternalLinkage (not CommonLinkage): COMMON TLS symbols are rejected by
    // the Linux module loader; the explicit zero init makes this a .tbss symbol.
    m_g_thread_state_tls = new GlobalVariable(
        m_module, i8_ptr, false, GlobalValue::InternalLinkage,
        ConstantPointerNull::get(i8_ptr), "__ang_rt_thread_state");
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
    // __ang_rt_alloc(i64 size, i32 type) -> i8*
    // Routes through the Allocator vtable: load alloc fn, call it, init header.
    // ========================================================================
    {
        auto* fn = createRuntimeFunc("__ang_rt_alloc",
            FunctionType::get(i8_ptr, {i64_ty, i32_ty}, false));
        m_fn_rt_alloc = FunctionCallee(fn);

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
        b.CreateStore(m_rt_initial_meta, b.CreateStructGEP(m_obj_header_type, mem, 1));
        b.CreateStore(ConstantPointerNull::get(i8_ptr),
                       b.CreateStructGEP(m_obj_header_type, mem, 2));
        b.CreateRet(mem);
    }

    // --- __ang_rt_free(i8* obj, i64 size) -> void : routes through Allocator ---
    // Used by `drop` (Stage 2) and __ang_rt_finalize.
    {
        auto* fn = createRuntimeFunc("__ang_rt_free",
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

    // --- __ang_rt_read_barrier(i8*) -> i8* : identity ---
    {
        auto* fn = createRuntimeFunc("__ang_rt_read_barrier",
            FunctionType::get(i8_ptr, {i8_ptr}, false));
        m_fn_rt_read_barrier = FunctionCallee(fn);
        IRBuilder<>(BasicBlock::Create(m_ctx, "entry", fn)).CreateRet(fn->arg_begin());
    }

    // --- Thread register/unregister: set/clear TLS ---
    {
        auto* fn = createRuntimeFunc("__ang_rt_thread_register",
            FunctionType::get(void_ty, {i8_ptr}, false));
        m_fn_rt_thread_register = FunctionCallee(fn);
        IRBuilder<> b(BasicBlock::Create(m_ctx, "entry", fn));
        b.CreateStore(fn->arg_begin(), m_g_thread_state_tls);
        b.CreateRetVoid();
    }
    {
        auto* fn = createRuntimeFunc("__ang_rt_thread_unregister",
            FunctionType::get(void_ty, {i8_ptr}, false));
        m_fn_rt_thread_unregister = FunctionCallee(fn);
        IRBuilder<> b(BasicBlock::Create(m_ctx, "entry", fn));
        b.CreateStore(ConstantPointerNull::get(i8_ptr), m_g_thread_state_tls);
        b.CreateRetVoid();
    }

    // --- clear_unique(AngaraObject) : clears is_unique on a heap object ---
    // String literals and other shared objects call this so the in-place
    // concat fast path never mutates a shared buffer.
    {
        auto* fn = createRuntimeFunc("__ang_rt_clear_unique",
            FunctionType::get(void_ty, {m_angara_obj_type}, false));
        m_fn_rt_clear_unique = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);

        // Extract the heap pointer from the AngaraObject payload.
        auto* obj_arg = fn->arg_begin();
        auto* payload = b.CreateExtractValue(obj_arg, {1}, "payload");
        auto* ptr = b.CreateIntToPtr(payload, i8_ptr, "obj_ptr");

        // Load the meta field (ObjHeader field 1).
        auto* meta = b.CreateLoad(i32_ty,
            b.CreateStructGEP(m_obj_header_type, ptr, 1), "meta");

        // Clear bit 8 (is_unique = 1 << 8 = 0x100).
        uint32_t mask = ~(1u << 8);  // 0xFFFFFEFF
        auto* cleared = b.CreateAnd(meta,
            ConstantInt::get(i32_ty, mask), "meta_cleared");
        b.CreateStore(cleared, b.CreateStructGEP(m_obj_header_type, ptr, 1));
        b.CreateRetVoid();
    }

    // --- No-op stubs ---
    stub_void("__ang_rt_collect",       m_fn_rt_collect);
    stub_void("__ang_rt_safepoint",     m_fn_rt_safepoint);
    stub_void("__ang_rt_pop_frame",     m_fn_rt_pop_frame);
    stub_void("__ang_rt_print_stats",   m_fn_rt_print_stats);
    stub_void_ptr("__ang_rt_push_frame", m_fn_rt_push_frame);

    // pin/unpin take AngaraObject (not i8*)
    {
        auto* fn = createRuntimeFunc("__ang_rt_pin",
            FunctionType::get(void_ty, {m_angara_obj_type}, false));
        m_fn_rt_pin = FunctionCallee(fn);
        IRBuilder<>(BasicBlock::Create(m_ctx, "entry", fn)).CreateRetVoid();
    }
    {
        auto* fn = createRuntimeFunc("__ang_rt_unpin",
            FunctionType::get(void_ty, {m_angara_obj_type}, false));
        m_fn_rt_unpin = FunctionCallee(fn);
        IRBuilder<>(BasicBlock::Create(m_ctx, "entry", fn)).CreateRetVoid();
    }

    // ========================================================================
    // __ang_rt_finalize(i8* obj) -> void
    // Recursively frees interior pointers of built-in heap types.
    // Called before __ang_rt_free in cgDrop so that strdup'd chars buffers,
    // list element arrays, record entry arrays, etc. are freed rather than
    // leaked.  The parent struct itself is freed by the subsequent __ang_rt_free.
    // ========================================================================
    {
        auto* fn = createRuntimeFunc("__ang_rt_finalize",
            FunctionType::get(void_ty, {i8_ptr}, false));
        m_fn_rt_finalize = FunctionCallee(fn);

        auto* obj_arg = fn->arg_begin();

        // --- basic blocks ---
        auto* entry_bb     = BasicBlock::Create(m_ctx, "entry", fn);
        auto* done_bb      = BasicBlock::Create(m_ctx, "done", fn);

        // Per-type basic blocks
        auto* string_bb    = BasicBlock::Create(m_ctx, "finalize_string", fn);
        auto* list_bb      = BasicBlock::Create(m_ctx, "finalize_list", fn);
        auto* record_bb    = BasicBlock::Create(m_ctx, "finalize_record", fn);
        auto* closure_bb   = BasicBlock::Create(m_ctx, "finalize_closure", fn);
        auto* raw_array_bb = BasicBlock::Create(m_ctx, "finalize_raw_array", fn);
        auto* thread_bb    = BasicBlock::Create(m_ctx, "finalize_thread", fn);
        auto* native_bb    = BasicBlock::Create(m_ctx, "finalize_native", fn);

        auto* free_chars_bb   = BasicBlock::Create(m_ctx, "free_chars", fn);
        auto* free_elems_bb   = BasicBlock::Create(m_ctx, "free_elems", fn);
        auto* rec_loop_check  = BasicBlock::Create(m_ctx, "rec_loop_check", fn);
        auto* rec_free_key    = BasicBlock::Create(m_ctx, "rec_free_key", fn);
        auto* free_entries_bb = BasicBlock::Create(m_ctx, "free_entries", fn);
        auto* free_env_bb     = BasicBlock::Create(m_ctx, "free_env", fn);
        auto* free_buf_bb     = BasicBlock::Create(m_ctx, "free_buf", fn);
        auto* free_args_bb    = BasicBlock::Create(m_ctx, "free_args", fn);
        auto* nat_finalize_bb = BasicBlock::Create(m_ctx, "nat_finalize", fn);
        auto* free_name_bb    = BasicBlock::Create(m_ctx, "free_name", fn);

        auto* free_fn = m_module.getFunction("free");

        // --- entry: load type tag and switch ---
        {
            IRBuilder<> b(entry_bb);
            auto* header_ptr = b.CreateBitCast(obj_arg,
                PointerType::get(m_ctx, 0), "header_ptr");
            auto* type_val = b.CreateLoad(Type::getInt32Ty(m_ctx),
                b.CreateStructGEP(m_obj_header_type, header_ptr, 0), "type");
            auto* sw = b.CreateSwitch(type_val, done_bb, 7);
            sw->addCase(ConstantInt::get(Type::getInt32Ty(m_ctx), OBJ_STRING),       string_bb);
            sw->addCase(ConstantInt::get(Type::getInt32Ty(m_ctx), OBJ_LIST),         list_bb);
            sw->addCase(ConstantInt::get(Type::getInt32Ty(m_ctx), OBJ_RECORD),       record_bb);
            sw->addCase(ConstantInt::get(Type::getInt32Ty(m_ctx), OBJ_CLOSURE),      closure_bb);
            sw->addCase(ConstantInt::get(Type::getInt32Ty(m_ctx), OBJ_RAW_ARRAY),    raw_array_bb);
            sw->addCase(ConstantInt::get(Type::getInt32Ty(m_ctx), OBJ_THREAD),       thread_bb);
            sw->addCase(ConstantInt::get(Type::getInt32Ty(m_ctx), OBJ_NATIVE_INSTANCE), native_bb);
        }

        // --- OBJ_STRING: free(chars) ---
        {
            IRBuilder<> b(string_bb);
            auto* str_ptr = b.CreateBitCast(obj_arg,
                PointerType::get(m_ctx, 0), "str_ptr");
            auto* chars = b.CreateLoad(PointerType::get(m_ctx, 0),
                b.CreateStructGEP(m_string_type, str_ptr, 3), "chars");
            auto* not_null = b.CreateIsNotNull(chars);
            b.CreateCondBr(not_null, free_chars_bb, done_bb);

            IRBuilder<> b2(free_chars_bb);
            b2.CreateCall(free_fn, {chars});
            b2.CreateBr(done_bb);
        }

        // --- OBJ_LIST: free(elements) ---
        {
            IRBuilder<> b(list_bb);
            auto* list_ptr = b.CreateBitCast(obj_arg,
                PointerType::get(m_ctx, 0), "list_ptr");
            auto* elems = b.CreateLoad(PointerType::get(m_ctx, 0),
                b.CreateStructGEP(m_list_type, list_ptr, 3), "elems");
            auto* not_null = b.CreateIsNotNull(elems);
            b.CreateCondBr(not_null, free_elems_bb, done_bb);

            IRBuilder<> b2(free_elems_bb);
            b2.CreateCall(free_fn, {elems});
            b2.CreateBr(done_bb);
        }

        // --- OBJ_RECORD: for each entry free(key), then free(entries) ---
        {
            IRBuilder<> b(record_bb);
            auto* rec_ptr = b.CreateBitCast(obj_arg,
                PointerType::get(m_ctx, 0), "rec_ptr");
            auto* entries = b.CreateLoad(PointerType::get(m_ctx, 0),
                b.CreateStructGEP(m_record_type, rec_ptr, 3), "entries");
            auto* count = b.CreateLoad(Type::getInt64Ty(m_ctx),
                b.CreateStructGEP(m_record_type, rec_ptr, 1), "count");
            auto* has_entries = b.CreateIsNotNull(entries);
            b.CreateCondBr(has_entries, rec_loop_check, done_bb);

            // Loop: free each key
            IRBuilder<> blc(rec_loop_check);
            auto* phi = blc.CreatePHI(Type::getInt64Ty(m_ctx), 2, "i");
            phi->addIncoming(ConstantInt::get(Type::getInt64Ty(m_ctx), 0), record_bb);
            auto* done_cond = blc.CreateICmpEQ(phi, count);
            blc.CreateCondBr(done_cond, free_entries_bb, rec_free_key);

            IRBuilder<> blf(rec_free_key);
            auto* entries_typed = blf.CreateBitCast(entries,
                PointerType::get(m_ctx, 0), "entries_typed");
            auto* entry_ptr = blf.CreateGEP(m_record_entry_type, entries_typed, {phi});
            auto* key = blf.CreateLoad(PointerType::get(m_ctx, 0),
                blf.CreateStructGEP(m_record_entry_type, entry_ptr, 0), "key");
            blf.CreateCall(free_fn, {key});
            auto* next_i = blf.CreateAdd(phi, ConstantInt::get(Type::getInt64Ty(m_ctx), 1));
            phi->addIncoming(next_i, rec_free_key);
            blf.CreateBr(rec_loop_check);

            IRBuilder<> bfe(free_entries_bb);
            bfe.CreateCall(free_fn, {entries});
            bfe.CreateBr(done_bb);
        }

        // --- OBJ_CLOSURE: free(env) ---
        {
            IRBuilder<> b(closure_bb);
            auto* clo_ptr = b.CreateBitCast(obj_arg,
                PointerType::get(m_ctx, 0), "clo_ptr");
            auto* env = b.CreateLoad(PointerType::get(m_ctx, 0),
                b.CreateStructGEP(m_closure_type, clo_ptr, 4), "env");
            auto* not_null = b.CreateIsNotNull(env);
            b.CreateCondBr(not_null, free_env_bb, done_bb);

            IRBuilder<> b2(free_env_bb);
            b2.CreateCall(free_fn, {env});
            b2.CreateBr(done_bb);
        }

        // --- OBJ_RAW_ARRAY: free(buf) ---
        {
            IRBuilder<> b(raw_array_bb);
            auto* ra_ptr = b.CreateBitCast(obj_arg,
                PointerType::get(m_ctx, 0), "ra_ptr");
            auto* buf = b.CreateLoad(PointerType::get(m_ctx, 0),
                b.CreateStructGEP(m_raw_array_type, ra_ptr, 4), "buf");
            auto* not_null = b.CreateIsNotNull(buf);
            b.CreateCondBr(not_null, free_buf_bb, done_bb);

            IRBuilder<> b2(free_buf_bb);
            b2.CreateCall(free_fn, {buf});
            b2.CreateBr(done_bb);
        }

        // --- OBJ_THREAD: free(args) ---
        {
            IRBuilder<> b(thread_bb);
            auto* thr_ptr = b.CreateBitCast(obj_arg,
                PointerType::get(m_ctx, 0), "thr_ptr");
            auto* args = b.CreateLoad(PointerType::get(m_ctx, 0),
                b.CreateStructGEP(m_thread_type, thr_ptr, 4), "args");
            auto* not_null = b.CreateIsNotNull(args);
            b.CreateCondBr(not_null, free_args_bb, done_bb);

            IRBuilder<> b2(free_args_bb);
            b2.CreateCall(free_fn, {args});
            b2.CreateBr(done_bb);
        }

        // --- OBJ_NATIVE_INSTANCE: if finalize!=null call finalize(data); free(name) ---
        {
            IRBuilder<> b(native_bb);
            auto* nat_ptr = b.CreateBitCast(obj_arg,
                PointerType::get(m_ctx, 0), "nat_ptr");
            // field 1: data, field 2: finalize callback, field 3: name
            auto* data_val = b.CreateLoad(PointerType::get(m_ctx, 0),
                b.CreateStructGEP(m_native_instance_type, nat_ptr, 1), "data");
            auto* fini_val = b.CreateLoad(PointerType::get(m_ctx, 0),
                b.CreateStructGEP(m_native_instance_type, nat_ptr, 2), "finalize");
            auto* name_val = b.CreateLoad(PointerType::get(m_ctx, 0),
                b.CreateStructGEP(m_native_instance_type, nat_ptr, 3), "name");
            auto* has_fini = b.CreateIsNotNull(fini_val);
            b.CreateCondBr(has_fini, nat_finalize_bb, free_name_bb);

            // Call the finalizer callback: void (*finalize)(void*)
            {
                IRBuilder<> b2(nat_finalize_bb);
                auto* finalize_ty = FunctionType::get(void_ty, {PointerType::get(m_ctx, 0)}, false);
                b2.CreateCall(finalize_ty, fini_val, {data_val});
                b2.CreateBr(free_name_bb);
            }

            IRBuilder<> b3(free_name_bb);
            auto* has_name = b3.CreateIsNotNull(name_val);
            auto* free_name_bb2 = BasicBlock::Create(m_ctx, "free_name_val", fn);
            b3.CreateCondBr(has_name, free_name_bb2, done_bb);

            IRBuilder<> b4(free_name_bb2);
            b4.CreateCall(free_fn, {name_val});
            b4.CreateBr(done_bb);
        }

        // --- done ---
        IRBuilder<>(done_bb).CreateRetVoid();
    }

    // --- obj_size: return 0 (unused without compaction) ---
    {
        auto* fn = createRuntimeFunc("__ang_rt_obj_size",
            FunctionType::get(i64_ty, {i8_ptr}, false));
        m_fn_rt_obj_size = FunctionCallee(fn);
        IRBuilder<>(BasicBlock::Create(m_ctx, "entry", fn))
            .CreateRet(ConstantInt::get(i64_ty, 0));
    }
}

} // namespace angara
