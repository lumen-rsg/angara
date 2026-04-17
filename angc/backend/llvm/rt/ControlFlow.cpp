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
        // Print error and exit
        auto* msg = ba.CreateGlobalString("Unhandled exception\n");
        auto* fprintf_fn = m_module.getFunction("fprintf");
        // stderr is typically at a fixed address, but we can't easily get it.
        // Use printf instead
        ba.CreateCall(m_module.getFunction("printf"), {msg});
        ba.CreateCall(m_module.getFunction("exit"), {ConstantInt::get(i32_ty, 1)});
        ba.CreateUnreachable();

        IRBuilder<> bu(unwind_bb);
        // Pop frame from chain
        // chain points to ExceptionFrame which is { jmp_buf, prev* }
        // We need to read prev and update chain, then longjmp
        // ExceptionFrame layout: first field is jmp_buf buffer, second is prev pointer
        // jmp_buf is opaque — we treat it as [200 x i8] (typical size)
        auto* frame_type = StructType::create(m_ctx, {
            ArrayType::get(i8_ty, 200),   // jmp_buf
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
            ArrayType::get(i8_ty, 200),
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
            ArrayType::get(i8_ty, 200),
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

    // Stubs that return nil
    auto* fn_ty = FunctionType::get(obj_ty, {obj_ty, i32_ty, PointerType::get(m_ctx, 0)}, false);
    auto* fn = createRuntimeFunc("__ang_spawn_thread", fn_ty);
    m_fn_thread_spawn = FunctionCallee(fn);
    {
        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        Value* nil_val = UndefValue::get(obj_ty);
        nil_val = b.CreateInsertValue(nil_val, ConstantInt::get(i32_ty, TAG_NIL), {0});
        nil_val = b.CreateInsertValue(nil_val,
            ConstantInt::get(i64_ty, 0), {1});
        b.CreateRet(nil_val);
    }

    auto* fn_ty2 = FunctionType::get(obj_ty, {obj_ty}, false);
    auto* fn2 = createRuntimeFunc("__ang_thread_join", fn_ty2);
    m_fn_thread_join = FunctionCallee(fn2);
    {
        auto* entry = BasicBlock::Create(m_ctx, "entry", fn2);
        IRBuilder<> b(entry);
        Value* nil_val = UndefValue::get(obj_ty);
        nil_val = b.CreateInsertValue(nil_val, ConstantInt::get(i32_ty, TAG_NIL), {0});
        nil_val = b.CreateInsertValue(nil_val,
            ConstantInt::get(i64_ty, 0), {1});
        b.CreateRet(nil_val);
    }

    auto* fn_ty3 = FunctionType::get(obj_ty, {}, false);
    auto* fn3 = createRuntimeFunc("__ang_mutex_new", fn_ty3);
    m_fn_mutex_new = FunctionCallee(fn3);
    {
        auto* entry = BasicBlock::Create(m_ctx, "entry", fn3);
        IRBuilder<> b(entry);
        Value* nil_val = UndefValue::get(obj_ty);
        nil_val = b.CreateInsertValue(nil_val, ConstantInt::get(i32_ty, TAG_NIL), {0});
        nil_val = b.CreateInsertValue(nil_val,
            ConstantInt::get(i64_ty, 0), {1});
        b.CreateRet(nil_val);
    }

    auto* void_ty = Type::getVoidTy(m_ctx);
    auto* fn_ty4 = FunctionType::get(void_ty, {obj_ty}, false);
    auto* fn4 = createRuntimeFunc("__ang_mutex_lock", fn_ty4);
    m_fn_mutex_lock = FunctionCallee(fn4);
    {
        auto* entry = BasicBlock::Create(m_ctx, "entry", fn4);
        IRBuilder<> b(entry);
        b.CreateRetVoid();
    }

    auto* fn5 = createRuntimeFunc("__ang_mutex_unlock", fn_ty4);
    m_fn_mutex_unlock = FunctionCallee(fn5);
    {
        auto* entry = BasicBlock::Create(m_ctx, "entry", fn5);
        IRBuilder<> b(entry);
        b.CreateRetVoid();
    }
}

} // namespace angara
