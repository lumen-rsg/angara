#include "RuntimeBuilder.h"
#include "MarkSweepGC.h"

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

    {
        auto* fn_ptr_type = PointerType::get(m_ctx, 0);
        auto* fn_ty = FunctionType::get(obj_ty, {fn_ptr_type, i32_ty, i1_ty, i8_ptr, i32_ty}, false);
        auto* fn = createRuntimeFunc("__ang_closure_new", fn_ty);
        m_fn_closure_new = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* fn_arg = fn->arg_begin();
        auto* arity_arg = fn->arg_begin() + 1;
        auto* native_arg = fn->arg_begin() + 2;
        auto* env_arg = fn->arg_begin() + 3;
        auto* env_count_arg = fn->arg_begin() + 4;

        auto* closure_size = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(m_closure_type));
        auto* mem = b.CreateCall(malloc_fn, {closure_size});
        auto* closure_ptr = b.CreateBitCast(mem, PointerType::get(m_ctx, 0));

        auto* header_ptr = b.CreateStructGEP(m_closure_type, closure_ptr, 0);
        b.CreateStore(ConstantInt::get(i32_ty, OBJ_CLOSURE),
            b.CreateStructGEP(m_obj_header_type, header_ptr, 0));
        b.CreateStore(ConstantInt::get(i32_ty, MarkSweepGC::packMeta(MarkSweepGC::COLOR_WHITE, true)),
            b.CreateStructGEP(m_obj_header_type, header_ptr, 1));
        b.CreateStore(ConstantPointerNull::get(PointerType::get(m_ctx, 0)),
            b.CreateStructGEP(m_obj_header_type, header_ptr, 2));
        b.CreateStore(fn_arg, b.CreateStructGEP(m_closure_type, closure_ptr, 1));
        b.CreateStore(arity_arg, b.CreateStructGEP(m_closure_type, closure_ptr, 2));
        b.CreateStore(native_arg, b.CreateStructGEP(m_closure_type, closure_ptr, 3));
        b.CreateStore(env_arg, b.CreateStructGEP(m_closure_type, closure_ptr, 4));
        b.CreateStore(env_count_arg, b.CreateStructGEP(m_closure_type, closure_ptr, 5));

        b.CreateRet(pack_obj(b, closure_ptr));
    }

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

        IRBuilder<> bc(is_closure_bb);
        auto* closure_ptr = bc.CreateIntToPtr(ptr_i64, PointerType::get(m_ctx, 0));
        auto* fn_field = bc.CreateLoad(
            PointerType::get(m_ctx, 0),
            bc.CreateStructGEP(m_closure_type, closure_ptr, 1), "fn");
        auto* env_field = bc.CreateLoad(
            PointerType::get(m_ctx, 0),
            bc.CreateStructGEP(m_closure_type, closure_ptr, 4), "env");
        auto* closure_fn_ty = FunctionType::get(obj_ty, {i32_ty, PointerType::get(m_ctx, 0), PointerType::get(m_ctx, 0)}, false);
        auto* result = bc.CreateCall(closure_fn_ty, fn_field, {argc, args, env_field});
        bc.CreateRet(result);

        IRBuilder<> bbm(is_bound_bb);
        auto* bm_ptr = bbm.CreateIntToPtr(ptr_i64, PointerType::get(m_ctx, 0));
        auto* receiver = bbm.CreateLoad(obj_ty, bbm.CreateStructGEP(m_bound_method_type, bm_ptr, 1));
        auto* method = bbm.CreateLoad(obj_ty, bbm.CreateStructGEP(m_bound_method_type, bm_ptr, 2));

        auto* new_argc = bbm.CreateAdd(argc, ConstantInt::get(i32_ty, 1));
        auto* elem_size = ConstantInt::get(i64_ty,
            m_module.getDataLayout().getTypeAllocSize(obj_ty));
        auto* new_args_size = bbm.CreateSExt(new_argc, i64_ty);
        auto* alloc_size = bbm.CreateMul(new_args_size, elem_size);
        auto* new_args = bbm.CreateCall(malloc_fn, {alloc_size});
        auto* new_args_typed = bbm.CreateBitCast(new_args, PointerType::get(m_ctx, 0));
        bbm.CreateStore(receiver, new_args_typed);
        bbm.CreateCall(m_module.getFunction("memcpy"),
            {bbm.CreateGEP(obj_ty, new_args_typed, {ConstantInt::get(i64_ty, 1)}),
             bbm.CreateBitCast(args, i8_ptr),
             bbm.CreateMul(bbm.CreateSExt(argc, i64_ty), elem_size)});

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

        IRBuilder<> be(error_bb);
        Value* nil_val = UndefValue::get(obj_ty);
        nil_val = be.CreateInsertValue(nil_val, ConstantInt::get(i32_ty, TAG_NIL), {0});
        nil_val = be.CreateInsertValue(nil_val,
            ConstantInt::get(i64_ty, 0), {1});
        be.CreateRet(nil_val);
    }

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
        b.CreateStore(ConstantInt::get(i32_ty, MarkSweepGC::packMeta(MarkSweepGC::COLOR_WHITE, true)),
            b.CreateStructGEP(m_obj_header_type, header_ptr, 1));
        b.CreateStore(ConstantPointerNull::get(PointerType::get(m_ctx, 0)),
            b.CreateStructGEP(m_obj_header_type, header_ptr, 2));
        b.CreateStore(recv_arg, b.CreateStructGEP(m_bound_method_type, bm_ptr, 1));
        b.CreateStore(closure_arg, b.CreateStructGEP(m_bound_method_type, bm_ptr, 2));

        b.CreateCall(m_module.getFunction("__ang_incref"), {recv_arg});
        b.CreateCall(m_module.getFunction("__ang_incref"), {closure_arg});

        b.CreateRet(pack_obj(b, bm_ptr));
    }
}

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
        b.CreateStore(ConstantInt::get(i32_ty, MarkSweepGC::packMeta(MarkSweepGC::COLOR_WHITE, true)),
            b.CreateStructGEP(m_obj_header_type, header_ptr, 1));
        b.CreateStore(ConstantPointerNull::get(PointerType::get(m_ctx, 0)),
            b.CreateStructGEP(m_obj_header_type, header_ptr, 2));
        b.CreateStore(msg, b.CreateStructGEP(m_exception_type, exc_ptr, 1));
        b.CreateCall(m_module.getFunction("__ang_incref"), {msg});

        b.CreateRet(pack_obj(b, exc_ptr));
    }

    {
        auto* fn_ty = FunctionType::get(obj_ty, {obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_exception_get_message", fn_ty);
        m_fn_exception_get_message = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* exc = fn->arg_begin();

        auto* payload = b.CreateExtractValue(exc, {1});
        auto* exc_ptr = b.CreateIntToPtr(payload, PointerType::get(m_ctx, 0));

        auto* msg = b.CreateLoad(obj_ty,
            b.CreateStructGEP(m_exception_type, exc_ptr, 1), "msg");
        b.CreateRet(msg);
    }

    {
        auto* fn_ty = FunctionType::get(Type::getVoidTy(m_ctx), {obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_throw", fn_ty);
        m_fn_throw = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* exc = fn->arg_begin();

        b.CreateStore(exc, m_g_current_exception);

        auto* chain = b.CreateLoad(i8_ptr, m_g_exception_chain, "chain");
        auto* is_null = b.CreateICmpEQ(chain, ConstantPointerNull::get(i8_ptr));

        auto* abort_bb = BasicBlock::Create(m_ctx, "abort", fn);
        auto* unwind_bb = BasicBlock::Create(m_ctx, "unwind", fn);
        b.CreateCondBr(is_null, abort_bb, unwind_bb);

        IRBuilder<> ba(abort_bb);
        auto* msg = ba.CreateGlobalString(
            "\033[1m\033[31m-> FATAL\033[0m\n"
            "\033[1m\033[31m   Unhandled exception was thrown but wasn't caught by any exception handlers. "
            "Terminating. (No active try / catch blocks found)\033[0m\n");
        ba.CreateCall(m_module.getFunction("printf"), {msg});
        ba.CreateCall(m_module.getFunction("exit"), {ConstantInt::get(i32_ty, 1)});
        ba.CreateUnreachable();

        IRBuilder<> bu(unwind_bb);
        auto* frame_type = StructType::create(m_ctx, {
            ArrayType::get(i8_ty, 512),
            i8_ptr
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

    {
        auto* fn_ty = FunctionType::get(i32_ty, {i8_ptr}, false);
        auto* fn = createRuntimeFunc("__ang_try_begin", fn_ty);
        m_fn_try_begin = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* frame_arg = fn->arg_begin();

        auto* frame_type = StructType::create(m_ctx, {
            ArrayType::get(i8_ty, 512),
            i8_ptr
        }, "ExceptionFrame");

        auto* frame = b.CreateBitCast(frame_arg, PointerType::get(m_ctx, 0));
        auto* prev_addr = b.CreateStructGEP(frame_type, frame, 1);
        auto* old_chain = b.CreateLoad(i8_ptr, m_g_exception_chain, "old_chain");
        b.CreateStore(old_chain, prev_addr);
        b.CreateStore(frame_arg, m_g_exception_chain);

        auto* jmp_buf_ptr = b.CreateStructGEP(frame_type, frame, 0);
        auto* result = b.CreateCall(m_module.getFunction("setjmp"),
            {b.CreateBitCast(jmp_buf_ptr, i8_ptr)}, "setjmp_result");
        b.CreateRet(result);
    }

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

    auto pack_obj = [&](IRBuilder<>& b, Value* raw_ptr) -> Value* {
        auto* ptr_i8 = b.CreateBitCast(raw_ptr, ptr_ty);
        auto* ptr_i64 = b.CreatePtrToInt(ptr_i8, i64_ty);
        Value* result = UndefValue::get(obj_ty);
        result = b.CreateInsertValue(result, ConstantInt::get(i32_ty, TAG_OBJ), {0});
        result = b.CreateInsertValue(result, ptr_i64, {1});
        return result;
    };

    auto make_nil = [&](IRBuilder<>& b) -> Value* {
        Value* nil_val = UndefValue::get(obj_ty);
        nil_val = b.CreateInsertValue(nil_val, ConstantInt::get(i32_ty, TAG_NIL), {0});
        nil_val = b.CreateInsertValue(nil_val, ConstantInt::get(i64_ty, 0), {1});
        return nil_val;
    };

    {
        auto* fn_ty = FunctionType::get(ptr_ty, {ptr_ty}, false);
        auto* trampoline = createRuntimeFunc("__ang_thread_trampoline", fn_ty);
        auto* entry = BasicBlock::Create(m_ctx, "entry", trampoline);
        IRBuilder<> b(entry);
        auto* arg = trampoline->arg_begin();
        auto* closure = b.CreateLoad(obj_ty, b.CreateStructGEP(m_thread_type, arg, 2), "closure");
        auto* argc = b.CreateLoad(i32_ty, b.CreateStructGEP(m_thread_type, arg, 3), "argc");
        auto* args = b.CreateLoad(ptr_ty, b.CreateStructGEP(m_thread_type, arg, 4), "args");
        auto* call_fn = m_module.getFunction("__ang_call");
        auto* result = b.CreateCall(call_fn, {closure, argc, args});
        b.CreateStore(result, b.CreateStructGEP(m_thread_type, arg, 2));
        // Free the heap-allocated args array (if any)
        auto* args_not_null = b.CreateICmpNE(args, ConstantPointerNull::get(ptr_ty));
        auto* free_bb = BasicBlock::Create(m_ctx, "free_args", trampoline);
        auto* done_bb = BasicBlock::Create(m_ctx, "done", trampoline);
        b.CreateCondBr(args_not_null, free_bb, done_bb);
        IRBuilder<> bf(free_bb);
        bf.CreateCall(free_fn, {args});
        bf.CreateBr(done_bb);
        IRBuilder<> bd(done_bb);
        bd.CreateRet(ConstantPointerNull::get(ptr_ty));
    }

    {
        auto* fn_ty = FunctionType::get(obj_ty, {obj_ty, i32_ty, ptr_ty}, false);
        auto* fn = createRuntimeFunc("__ang_spawn_thread", fn_ty);
        m_fn_thread_spawn = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* closure = fn->arg_begin();

        auto* size = ConstantInt::get(i64_ty, 56);
        auto* mem = b.CreateCall(malloc_fn, {size}, "mem");
        auto* thread_ptr = b.CreateBitCast(mem, ptr_ty, "thread_ptr");

        auto* header = b.CreateStructGEP(m_thread_type, thread_ptr, 0);
        auto* type_addr = b.CreateStructGEP(m_obj_header_type, header, 0);
        b.CreateStore(ConstantInt::get(i32_ty, OBJ_THREAD), type_addr);
        auto* meta_addr = b.CreateStructGEP(m_obj_header_type, header, 1);
        b.CreateStore(ConstantInt::get(i32_ty, MarkSweepGC::packMeta(MarkSweepGC::COLOR_WHITE, true)), meta_addr);
        auto* next_addr = b.CreateStructGEP(m_obj_header_type, header, 2);
        b.CreateStore(ConstantPointerNull::get(ptr_ty), next_addr);

        auto* pthread_slot = b.CreateStructGEP(m_thread_type, thread_ptr, 1);
        b.CreateStore(ConstantPointerNull::get(ptr_ty), pthread_slot);

        auto* result_slot = b.CreateStructGEP(m_thread_type, thread_ptr, 2);
        b.CreateStore(closure, result_slot);

        // Store argc and args array for the spawned function
        auto* argc_arg = fn->arg_begin() + 1;
        auto* args_arg = fn->arg_begin() + 2;
        b.CreateStore(argc_arg, b.CreateStructGEP(m_thread_type, thread_ptr, 3));
        b.CreateStore(args_arg, b.CreateStructGEP(m_thread_type, thread_ptr, 4));

        auto* trampoline = m_module.getFunction("__ang_thread_trampoline");
        b.CreateCall(pthread_create_fn, {
            pthread_slot,
            ConstantPointerNull::get(ptr_ty),
            trampoline,
            thread_ptr
        });

        b.CreateRet(pack_obj(b, thread_ptr));
    }

    {
        auto* fn_ty = FunctionType::get(obj_ty, {obj_ty}, false);
        auto* fn = createRuntimeFunc("__ang_thread_join", fn_ty);
        m_fn_thread_join = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* thread_obj = fn->arg_begin();

        auto* payload = b.CreateExtractValue(thread_obj, {1});
        auto* ptr_i64 = b.CreateBitCast(payload, i64_ty);
        auto* thread_ptr = b.CreateIntToPtr(ptr_i64, ptr_ty);

        auto* pthread_val = b.CreateLoad(ptr_ty,
            b.CreateStructGEP(m_thread_type, thread_ptr, 1), "pthread");

        b.CreateCall(pthread_join_fn, {pthread_val, ConstantPointerNull::get(ptr_ty)});

        auto* result = b.CreateLoad(obj_ty,
            b.CreateStructGEP(m_thread_type, thread_ptr, 2), "result");
        b.CreateRet(result);
    }

    {
        auto* fn_ty = FunctionType::get(obj_ty, {}, false);
        auto* fn = createRuntimeFunc("__ang_mutex_new", fn_ty);
        m_fn_mutex_new = FunctionCallee(fn);

        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);

        auto* size = ConstantInt::get(i64_ty, 80);
        auto* mem = b.CreateCall(malloc_fn, {size}, "mem");
        auto* mutex_ptr = b.CreateBitCast(mem, ptr_ty, "mutex_ptr");

        auto* header = b.CreateStructGEP(m_mutex_type, mutex_ptr, 0);
        auto* type_addr = b.CreateStructGEP(m_obj_header_type, header, 0);
        b.CreateStore(ConstantInt::get(i32_ty, OBJ_MUTEX), type_addr);
        auto* meta_addr = b.CreateStructGEP(m_obj_header_type, header, 1);
        b.CreateStore(ConstantInt::get(i32_ty, MarkSweepGC::packMeta(MarkSweepGC::COLOR_WHITE, true)), meta_addr);
        auto* next_addr = b.CreateStructGEP(m_obj_header_type, header, 2);
        b.CreateStore(ConstantPointerNull::get(ptr_ty), next_addr);

        auto* mutex_bytes = b.CreateStructGEP(m_mutex_type, mutex_ptr, 1);

        b.CreateCall(pthread_mutex_init_fn, {mutex_bytes, ConstantPointerNull::get(ptr_ty)});

        b.CreateRet(pack_obj(b, mutex_ptr));
    }

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
