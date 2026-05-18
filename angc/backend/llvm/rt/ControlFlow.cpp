// Angara LLVM Backend — Runtime: Closures, Exceptions, Threads
#include "RuntimeBuilder.h"

using namespace llvm;

namespace angara {
void RuntimeBuilder::generateClosureOps() {
    auto* i8_ty = Type::getInt8Ty(m_ctx);
    auto* i32_ty = Type::getInt32Ty(m_ctx);
    auto* i64_ty = Type::getInt64Ty(m_ctx);
    auto* i1_ty = Type::getInt1Ty(m_ctx);
    auto* i8_ptr = PointerType::get(m_ctx, 0);
    auto* obj_ty = m_angara_obj_type;

    auto* malloc_fn = m_module.getFunction("malloc");

    auto pack_obj = [&](IRBuilder<>& b, Value* raw_ptr) -> Value* {
        auto* ptr_i8 = b.CreateBitCast(raw_ptr, i8_ptr);
        auto* ptr_i64 = b.CreatePtrToInt(ptr_i8, i64_ty);
        auto* payload = ptr_i64;
        Value* result = UndefValue::get(obj_ty);
        result = b.CreateInsertValue(result, ConstantInt::get(i32_ty, TAG_OBJ), {0});
        result = b.CreateInsertValue(result, payload, {1});
        return result;
    };

    // --- closure_new(void* fn, i32 arity, i1 is_native) -> AngaraObject ---
    {
        auto* fn_ptr_type = PointerType::get(m_ctx, 0);
        auto* fn_ty = FunctionType::get(obj_ty, {fn_ptr_type, i32_ty, i1_ty}, false);
        auto* fn = createRuntimeFunc("__ang_closure_new", fn_ty);
        m_fn_closure_new = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* fn_arg = fn->arg_begin();
        auto* arity_arg = fn->arg_begin() + 1;
        auto* native_arg = fn->arg_begin() + 2;

        auto* closure_size = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(m_closure_type));
        auto* mem = b.CreateCall(malloc_fn, {closure_size});
        auto* closure_ptr = b.CreateBitCast(mem, PointerType::get(m_ctx, 0));

        auto* header_ptr = b.CreateStructGEP(m_closure_type, closure_ptr, 0);
        b.CreateStore(ConstantInt::get(i32_ty, OBJ_CLOSURE),
            b.CreateStructGEP(m_obj_header_type, header_ptr, 0));
        b.CreateStore(ConstantInt::get(i64_ty, 1),
            b.CreateStructGEP(m_obj_header_type, header_ptr, 1));
        b.CreateStore(fn_arg, b.CreateStructGEP(m_closure_type, closure_ptr, 1));
        b.CreateStore(arity_arg, b.CreateStructGEP(m_closure_type, closure_ptr, 2));
        b.CreateStore(native_arg, b.CreateStructGEP(m_closure_type, closure_ptr, 3));

        b.CreateRet(pack_obj(b, closure_ptr));
    }

    // --- call(AngaraObject callee, i32 argc, AngaraObject* args) -> AngaraObject ---
    {
        auto* fn_ty = FunctionType::get(obj_ty, {obj_ty, i32_ty, PointerType::get(m_ctx, 0)}, false);
        auto* fn = createRuntimeFunc("__ang_call", fn_ty);
        m_fn_call = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        auto* is_closure_bb = BasicBlock::Create(m_ctx, "is_closure", fn);
        auto* is_bound_bb = BasicBlock::Create(m_ctx, "is_bound", fn);
        auto* error_bb = BasicBlock::Create(m_ctx, "error", fn);

        IRBuilder<> b(entry);
        auto* callee = fn->arg_begin();
        auto* argc = fn->arg_begin() + 1;
        auto* args = fn->arg_begin() + 2;

        auto* payload = b.CreateExtractValue(callee, {1});
        auto* ptr_i64 = b.CreateBitCast(payload, i64_ty);
        auto* obj_ptr = b.CreateIntToPtr(ptr_i64, PointerType::get(m_ctx, 0));
        auto* obj_type = b.CreateLoad(i32_ty, b.CreateStructGEP(m_obj_header_type, obj_ptr, 0));

        auto* sw = b.CreateSwitch(obj_type, error_bb, 2);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_CLOSURE), is_closure_bb);
        sw->addCase(ConstantInt::get(i32_ty, OBJ_BOUND_METHOD), is_bound_bb);

        // Closure call
        IRBuilder<> bc(is_closure_bb);
        auto* closure_ptr = bc.CreateIntToPtr(ptr_i64, PointerType::get(m_ctx, 0));
        auto* fn_field = bc.CreateLoad(
            PointerType::get(m_ctx, 0),
            bc.CreateStructGEP(m_closure_type, closure_ptr, 1), "fn");
        auto* closure_fn_ty = FunctionType::get(obj_ty, {i32_ty, PointerType::get(m_ctx, 0)}, false);
        auto* result = bc.CreateCall(closure_fn_ty, fn_field, {argc, args});
        bc.CreateRet(result);

        // Bound method call: prepend receiver to args
        IRBuilder<> bbm(is_bound_bb);
        auto* bm_ptr = bbm.CreateIntToPtr(ptr_i64, PointerType::get(m_ctx, 0));
        auto* receiver = bbm.CreateLoad(obj_ty, bbm.CreateStructGEP(m_bound_method_type, bm_ptr, 1));
        auto* method = bbm.CreateLoad(obj_ty, bbm.CreateStructGEP(m_bound_method_type, bm_ptr, 2));

        // Allocate new args array: argc + 1
        auto* new_argc = bbm.CreateAdd(argc, ConstantInt::get(i32_ty, 1));
        auto* elem_size = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(obj_ty));
        auto* new_args_size = bbm.CreateSExt(new_argc, i64_ty);
        auto* alloc_size = bbm.CreateMul(new_args_size, elem_size);
        auto* new_args = bbm.CreateCall(malloc_fn, {alloc_size});
        auto* new_args_typed = bbm.CreateBitCast(new_args, PointerType::get(m_ctx, 0));
        // Store receiver at [0]
        bbm.CreateStore(receiver, new_args_typed);
        // Copy original args starting at [1]
        bbm.CreateCall(m_module.getFunction("memcpy"),
            {bbm.CreateGEP(obj_ty, new_args_typed, {ConstantInt::get(i64_ty, 1)}),
             bbm.CreateBitCast(args, i8_ptr),
             bbm.CreateMul(bbm.CreateSExt(argc, i64_ty), elem_size)});

        // Get closure fn from method
        auto* m_payload = bbm.CreateExtractValue(method, {1});
        auto* m_ptr_i64 = bbm.CreateBitCast(m_payload, i64_ty);
        auto* m_closure_ptr = bbm.CreateIntToPtr(m_ptr_i64, PointerType::get(m_ctx, 0));
        auto* m_fn = bbm.CreateLoad(
            PointerType::get(m_ctx, 0),
            bbm.CreateStructGEP(m_closure_type, m_closure_ptr, 1));
        auto* method_fn_ty = FunctionType::get(obj_ty, {i32_ty, PointerType::get(m_ctx, 0)}, false);
        auto* call_result = bbm.CreateCall(method_fn_ty, m_fn, {new_argc, new_args_typed});
        bbm.CreateCall(m_module.getFunction("free"), {new_args});
        bbm.CreateRet(call_result);

        // Error: return nil
        IRBuilder<> be(error_bb);
        Value* nil_val = UndefValue::get(obj_ty);
        nil_val = be.CreateInsertValue(nil_val, ConstantInt::get(i32_ty, TAG_NIL), {0});
        nil_val = be.CreateInsertValue(nil_val,
            ConstantInt::get(i64_ty, 0), {1});
        be.CreateRet(nil_val);
    }

    // --- bound_method_new(AngaraObject receiver, AngaraObject closure) -> AngaraObject ---
    {
        auto* fn_ty = FunctionType::get(obj_ty, {obj_ty, obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_bound_method_new", fn_ty);
        m_fn_bound_method_new = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* recv_arg = fn->arg_begin();
        auto* closure_arg = fn->arg_begin() + 1;

        auto* bm_size = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(m_bound_method_type));
        auto* mem = b.CreateCall(malloc_fn, {bm_size});
        auto* bm_ptr = b.CreateBitCast(mem, PointerType::get(m_ctx, 0));

        auto* header_ptr = b.CreateStructGEP(m_bound_method_type, bm_ptr, 0);
        b.CreateStore(ConstantInt::get(i32_ty, OBJ_BOUND_METHOD),
            b.CreateStructGEP(m_obj_header_type, header_ptr, 0));
        b.CreateStore(ConstantInt::get(i64_ty, 1),
            b.CreateStructGEP(m_obj_header_type, header_ptr, 1));
        b.CreateStore(recv_arg, b.CreateStructGEP(m_bound_method_type, bm_ptr, 1));
        b.CreateStore(closure_arg, b.CreateStructGEP(m_bound_method_type, bm_ptr, 2));

        // Incref receiver and closure
        b.CreateCall(m_module.getFunction("__ang_incref"), {recv_arg});
        b.CreateCall(m_module.getFunction("__ang_incref"), {closure_arg});

        b.CreateRet(pack_obj(b, bm_ptr));
    }
}

// ============================================================================
// Exception Operations
// ============================================================================

void RuntimeBuilder::generateExceptionOps() {
    auto* i8_ty = Type::getInt8Ty(m_ctx);
    auto* i32_ty = Type::getInt32Ty(m_ctx);
    auto* i64_ty = Type::getInt64Ty(m_ctx);
    auto* i8_ptr = PointerType::get(m_ctx, 0);
    auto* obj_ty = m_angara_obj_type;

    auto* malloc_fn = m_module.getFunction("malloc");

    auto pack_obj = [&](IRBuilder<>& b, Value* raw_ptr) -> Value* {
        auto* ptr_i8 = b.CreateBitCast(raw_ptr, i8_ptr);
        auto* ptr_i64 = b.CreatePtrToInt(ptr_i8, i64_ty);
        auto* payload = ptr_i64;
        Value* result = UndefValue::get(obj_ty);
        result = b.CreateInsertValue(result, ConstantInt::get(i32_ty, TAG_OBJ), {0});
        result = b.CreateInsertValue(result, payload, {1});
        return result;
    };

    // --- exception_new(AngaraObject message) -> AngaraObject ---
    {
        auto* fn_ty = FunctionType::get(obj_ty, {obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_exception_new", fn_ty);
        m_fn_exception_new = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* msg = fn->arg_begin();

        auto* exc_size = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(m_exception_type));
        auto* mem = b.CreateCall(malloc_fn, {exc_size});
        auto* exc_ptr = b.CreateBitCast(mem, PointerType::get(m_ctx, 0));

        auto* header_ptr = b.CreateStructGEP(m_exception_type, exc_ptr, 0);
        b.CreateStore(ConstantInt::get(i32_ty, OBJ_EXCEPTION),
            b.CreateStructGEP(m_obj_header_type, header_ptr, 0));
        b.CreateStore(ConstantInt::get(i64_ty, 1),
            b.CreateStructGEP(m_obj_header_type, header_ptr, 1));
        b.CreateStore(msg, b.CreateStructGEP(m_exception_type, exc_ptr, 1));
        b.CreateCall(m_module.getFunction("__ang_incref"), {msg});

        b.CreateRet(pack_obj(b, exc_ptr));
    }

    // --- exception_get_message(AngaraObject exception) -> AngaraObject ---
    {
        auto* fn_ty = FunctionType::get(obj_ty, {obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_exception_get_message", fn_ty);
        m_fn_exception_get_message = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* exc = fn->arg_begin();

        // Extract pointer from object payload
        auto* payload = b.CreateExtractValue(exc, {1});
        auto* exc_ptr = b.CreateIntToPtr(payload, PointerType::get(m_ctx, 0));

        // AngaraException: { ObjHeader, AngaraObject message }
        // Load message from field index 1
        auto* msg = b.CreateLoad(obj_ty,
            b.CreateStructGEP(m_exception_type, exc_ptr, 1), "msg");
        b.CreateRet(msg);
    }

    // --- throw(AngaraObject exception) — calls longjmp ---
    {
        auto* fn_ty = FunctionType::get(Type::getVoidTy(m_ctx), {obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_throw", fn_ty);
        m_fn_throw = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* exc = fn->arg_begin();

        // Store exception in global
        b.CreateStore(exc, m_g_current_exception);

        // Load chain head, if null → abort
        auto* chain = b.CreateLoad(i8_ptr, m_g_exception_chain, "chain");
        auto* is_null = b.CreateICmpEQ(chain, ConstantPointerNull::get(i8_ptr));

        auto* abort_bb = BasicBlock::Create(m_ctx, "abort", fn);
        auto* unwind_bb = BasicBlock::Create(m_ctx, "unwind", fn);
        b.CreateCondBr(is_null, abort_bb, unwind_bb);

        IRBuilder<> ba(abort_bb);
        // Print bold red fatal error message and exit
        auto* msg = ba.CreateGlobalString(
            "\033[1m\033[31m-> FATAL\033[0m\n"
            "\033[1m\033[31m   Unhandled exception was thrown but wasn't caught by any exception handlers. "
            "Terminating. (No active try / catch blocks found)\033[0m\n");
        ba.CreateCall(m_module.getFunction("printf"), {msg});
        ba.CreateCall(m_module.getFunction("exit"), {ConstantInt::get(i32_ty, 1)});
        ba.CreateUnreachable();

        IRBuilder<> bu(unwind_bb);
        // Pop frame from chain
        // chain points to ExceptionFrame which is { jmp_buf, prev* }
        // We need to read prev and update chain, then longjmp
        // ExceptionFrame layout: first field is jmp_buf buffer, second is prev pointer
        // jmp_buf size varies by platform (192 on macOS ARM64, 200 on Linux x86_64, etc.)
        // Use 512 bytes to safely cover all known platforms
        auto* frame_type = StructType::create(m_ctx, {
            ArrayType::get(i8_ty, 512),   // jmp_buf (generously sized)
            i8_ptr                        // prev
        }, "ExceptionFrame");

        auto* frame = bu.CreateBitCast(chain, PointerType::get(m_ctx, 0));
        auto* prev_ptr = bu.CreateStructGEP(frame_type, frame, 1);
        auto* prev = bu.CreateLoad(i8_ptr, prev_ptr, "prev");
        bu.CreateStore(prev, m_g_exception_chain);

        auto* jmp_buf_ptr = bu.CreateStructGEP(frame_type, frame, 0);
        bu.CreateCall(m_module.getFunction("longjmp"), {
            bu.CreateBitCast(jmp_buf_ptr, i8_ptr),
            ConstantInt::get(i32_ty, 1)
        });
        bu.CreateUnreachable();
    }

    // --- try_begin(i8* frame) -> i32 (returns setjmp result) ---
    {
        auto* fn_ty = FunctionType::get(i32_ty, {i8_ptr}, false);
        auto* fn = createRuntimeFunc("__ang_try_begin", fn_ty);
        m_fn_try_begin = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* frame_arg = fn->arg_begin();

        // Push frame onto chain: frame->prev = chain_head, chain_head = frame
        auto* frame_type = StructType::create(m_ctx, {
            ArrayType::get(i8_ty, 512),
            i8_ptr
        }, "ExceptionFrame");

        auto* frame = b.CreateBitCast(frame_arg, PointerType::get(m_ctx, 0));
        auto* prev_addr = b.CreateStructGEP(frame_type, frame, 1);
        auto* old_chain = b.CreateLoad(i8_ptr, m_g_exception_chain, "old_chain");
        b.CreateStore(old_chain, prev_addr);
        b.CreateStore(frame_arg, m_g_exception_chain);

        // Call setjmp
        auto* jmp_buf_ptr = b.CreateStructGEP(frame_type, frame, 0);
        auto* result = b.CreateCall(m_module.getFunction("setjmp"),
            {b.CreateBitCast(jmp_buf_ptr, i8_ptr)}, "setjmp_result");
        b.CreateRet(result);
    }

    // --- try_end() — pops the exception frame ---
    {
        auto* fn_ty = FunctionType::get(Type::getVoidTy(m_ctx), {}, false);
        auto* fn = createRuntimeFunc("__ang_try_end", fn_ty);
        m_fn_try_end = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);

        auto* frame_type = StructType::create(m_ctx, {
            ArrayType::get(i8_ty, 512),
            i8_ptr
        }, "ExceptionFrame");

        auto* chain = b.CreateLoad(i8_ptr, m_g_exception_chain, "chain");
        auto* frame = b.CreateBitCast(chain, PointerType::get(m_ctx, 0));
        auto* prev = b.CreateLoad(i8_ptr, b.CreateStructGEP(frame_type, frame, 1), "prev");
        b.CreateStore(prev, m_g_exception_chain);
        b.CreateRetVoid();
    }
}

// ============================================================================
// Thread Operations (stubs)
// ============================================================================

void RuntimeBuilder::generateThreadOps() {
    auto* obj_ty = m_angara_obj_type;
    auto* i32_ty = Type::getInt32Ty(m_ctx);
    auto* i64_ty = Type::getInt64Ty(m_ctx);
    auto* i8_ty = Type::getInt8Ty(m_ctx);
    auto* void_ty = Type::getVoidTy(m_ctx);
    auto* ptr_ty = PointerType::get(m_ctx, 0);

    auto* malloc_fn = m_module.getFunction("malloc");
    auto* free_fn = m_module.getFunction("free");
    auto* pthread_create_fn = m_module.getFunction("pthread_create");
    auto* pthread_join_fn = m_module.getFunction("pthread_join");
    auto* pthread_mutex_init_fn = m_module.getFunction("pthread_mutex_init");
    auto* pthread_mutex_lock_fn = m_module.getFunction("pthread_mutex_lock");
    auto* pthread_mutex_unlock_fn = m_module.getFunction("pthread_mutex_unlock");

    // Helper: pack a heap pointer into AngaraObject with TAG_OBJ
    auto pack_obj = [&](IRBuilder<>& b, Value* raw_ptr) -> Value* {
        auto* ptr_i8 = b.CreateBitCast(raw_ptr, ptr_ty);
        auto* ptr_i64 = b.CreatePtrToInt(ptr_i8, i64_ty);
        Value* result = UndefValue::get(obj_ty);
        result = b.CreateInsertValue(result, ConstantInt::get(i32_ty, TAG_OBJ), {0});
        result = b.CreateInsertValue(result, ptr_i64, {1});
        return result;
    };

    // Helper: make nil
    auto make_nil = [&](IRBuilder<>& b) -> Value* {
        Value* nil_val = UndefValue::get(obj_ty);
        nil_val = b.CreateInsertValue(nil_val, ConstantInt::get(i32_ty, TAG_NIL), {0});
        nil_val = b.CreateInsertValue(nil_val, ConstantInt::get(i64_ty, 0), {1});
        return nil_val;
    };

    // ========================================================================
    // Thread trampoline: called by pthread_create, calls the closure, stores result
    //   define void* @__ang_thread_trampoline(ptr %arg) {
    //     %thread_struct = arg  -- points to AngaraThread
    //     %closure = load from thread_struct field 2 (result slot reused temporarily)
    //     %result = call __ang_call(%closure, 0, null)
    //     store %result into thread_struct field 2
    //     ret null
    //   }
    // ========================================================================
    {
        auto* fn_ty = FunctionType::get(ptr_ty, {ptr_ty}, false);
        auto* trampoline = createRuntimeFunc("__ang_thread_trampoline", fn_ty);
        auto* entry = BasicBlock::Create(m_ctx, "entry", trampoline);
        IRBuilder<> b(entry);
        auto* arg = trampoline->arg_begin();
        // The arg points to the AngaraThread struct
        // Field layout: 0=ObjHeader, 1=pthread_t, 2=AngaraObject (closure, then result)
        auto* closure = b.CreateLoad(obj_ty, b.CreateStructGEP(m_thread_type, arg, 2), "closure");
        auto* call_fn = m_module.getFunction("__ang_call");
        auto* result = b.CreateCall(call_fn, {
            closure,
            ConstantInt::get(i32_ty, 0),
            ConstantPointerNull::get(ptr_ty)
        });
        // Store result back into field 2
        b.CreateStore(result, b.CreateStructGEP(m_thread_type, arg, 2));
        b.CreateRet(ConstantPointerNull::get(ptr_ty));
    }

    // ========================================================================
    // __ang_spawn_thread(AngaraObject closure, i32 argc, ptr args) -> AngaraObject
    // Allocates AngaraThread, stores closure, calls pthread_create, returns thread obj
    // ========================================================================
    {
        auto* fn_ty = FunctionType::get(obj_ty, {obj_ty, i32_ty, ptr_ty}, false);
        auto* fn = createRuntimeFunc("__ang_spawn_thread", fn_ty);
        m_fn_thread_spawn = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* closure = fn->arg_begin();

        // Allocate AngaraThread struct
        auto* size = ConstantInt::get(i64_ty, 32); // header(16) + ptr(8) + obj(16) = 40, padded to 32 w/ alignment
        size = ConstantInt::get(i64_ty, 40);
        auto* mem = b.CreateCall(malloc_fn, {size}, "mem");
        auto* thread_ptr = b.CreateBitCast(mem, ptr_ty, "thread_ptr");

        // Set header: obj_type = OBJ_THREAD, ref_count = 1
        auto* header = b.CreateStructGEP(m_thread_type, thread_ptr, 0);
        auto* type_addr = b.CreateStructGEP(m_obj_header_type, header, 0);
        b.CreateStore(ConstantInt::get(i32_ty, OBJ_THREAD), type_addr);
        auto* rc_addr = b.CreateStructGEP(m_obj_header_type, header, 1);
        b.CreateStore(ConstantInt::get(i64_ty, 1), rc_addr);

        // pthread_t slot (field 1) = null initially
        auto* pthread_slot = b.CreateStructGEP(m_thread_type, thread_ptr, 1);
        b.CreateStore(ConstantPointerNull::get(ptr_ty), pthread_slot);

        // Store closure in field 2 (will be overwritten with result after thread runs)
        auto* result_slot = b.CreateStructGEP(m_thread_type, thread_ptr, 2);
        b.CreateStore(closure, result_slot);

        // pthread_create(&thread_ptr->pthread, null, __ang_thread_trampoline, thread_ptr)
        auto* trampoline = m_module.getFunction("__ang_thread_trampoline");
        b.CreateCall(pthread_create_fn, {
            pthread_slot,  // pthread_t* (output)
            ConstantPointerNull::get(ptr_ty),  // attr
            trampoline,    // start_routine
            thread_ptr     // arg
        });

        b.CreateRet(pack_obj(b, thread_ptr));
    }

    // ========================================================================
    // __ang_thread_join(AngaraObject thread) -> AngaraObject
    // Joins the pthread, returns the stored result from field 2
    // ========================================================================
    {
        auto* fn_ty = FunctionType::get(obj_ty, {obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_thread_join", fn_ty);
        m_fn_thread_join = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* thread_obj = fn->arg_begin();

        // Extract the heap pointer
        auto* payload = b.CreateExtractValue(thread_obj, {1});
        auto* ptr_i64 = b.CreateBitCast(payload, i64_ty);
        auto* thread_ptr = b.CreateIntToPtr(ptr_i64, ptr_ty);

        // Get pthread_t from field 1
        auto* pthread_val = b.CreateLoad(ptr_ty,
            b.CreateStructGEP(m_thread_type, thread_ptr, 1), "pthread");

        // pthread_join(pthread, null)
        b.CreateCall(pthread_join_fn, {pthread_val, ConstantPointerNull::get(ptr_ty)});

        // Load result from field 2
        auto* result = b.CreateLoad(obj_ty,
            b.CreateStructGEP(m_thread_type, thread_ptr, 2), "result");
        b.CreateRet(result);
    }

    // ========================================================================
    // __ang_mutex_new() -> AngaraObject
    // Allocates AngaraMutex, calls pthread_mutex_init, returns wrapped obj
    // ========================================================================
    {
        auto* fn_ty = FunctionType::get(obj_ty, {}, false);
        auto* fn = createRuntimeFunc("__ang_mutex_new", fn_ty);
        m_fn_mutex_new = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);

        // Allocate AngaraMutex struct
        auto* size = ConstantInt::get(i64_ty, 80); // header(16) + mutex_bytes(64) = 80
        auto* mem = b.CreateCall(malloc_fn, {size}, "mem");
        auto* mutex_ptr = b.CreateBitCast(mem, ptr_ty, "mutex_ptr");

        // Set header: obj_type = OBJ_MUTEX, ref_count = 1
        auto* header = b.CreateStructGEP(m_mutex_type, mutex_ptr, 0);
        auto* type_addr = b.CreateStructGEP(m_obj_header_type, header, 0);
        b.CreateStore(ConstantInt::get(i32_ty, OBJ_MUTEX), type_addr);
        auto* rc_addr = b.CreateStructGEP(m_obj_header_type, header, 1);
        b.CreateStore(ConstantInt::get(i64_ty, 1), rc_addr);

        // Get pointer to the mutex bytes (field 1)
        auto* mutex_bytes = b.CreateStructGEP(m_mutex_type, mutex_ptr, 1);

        // pthread_mutex_init(mutex_bytes, null)
        b.CreateCall(pthread_mutex_init_fn, {mutex_bytes, ConstantPointerNull::get(ptr_ty)});

        b.CreateRet(pack_obj(b, mutex_ptr));
    }

    // ========================================================================
    // __ang_mutex_lock(AngaraObject mutex) -> void
    // ========================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_mutex_lock", fn_ty);
        m_fn_mutex_lock = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* mutex_obj = fn->arg_begin();

        auto* payload = b.CreateExtractValue(mutex_obj, {1});
        auto* ptr_i64 = b.CreateBitCast(payload, i64_ty);
        auto* mutex_ptr = b.CreateIntToPtr(ptr_i64, ptr_ty);
        auto* mutex_bytes = b.CreateStructGEP(m_mutex_type, mutex_ptr, 1);

        b.CreateCall(pthread_mutex_lock_fn, {mutex_bytes});
        b.CreateRetVoid();
    }

    // ========================================================================
    // __ang_mutex_unlock(AngaraObject mutex) -> void
    // ========================================================================
    {
        auto* fn_ty = FunctionType::get(void_ty, {obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_mutex_unlock", fn_ty);
        m_fn_mutex_unlock = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* mutex_obj = fn->arg_begin();

        auto* payload = b.CreateExtractValue(mutex_obj, {1});
        auto* ptr_i64 = b.CreateBitCast(payload, i64_ty);
        auto* mutex_ptr = b.CreateIntToPtr(ptr_i64, ptr_ty);
        auto* mutex_bytes = b.CreateStructGEP(m_mutex_type, mutex_ptr, 1);

        b.CreateCall(pthread_mutex_unlock_fn, {mutex_bytes});
        b.CreateRetVoid();
    }
}

} // namespace angara
