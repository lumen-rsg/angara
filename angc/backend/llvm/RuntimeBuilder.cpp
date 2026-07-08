#include "RuntimeBuilder.h"
#include <llvm/IR/Verifier.h>
#include <llvm/IR/Constants.h>
#include <llvm/Support/raw_ostream.h>

using namespace llvm;

namespace angara {

RuntimeBuilder::RuntimeBuilder(LLVMContext& context, Module& module, IRBuilder<>& builder, bool freestanding, bool kernel, unsigned jmp_buf_size)
    : m_ctx(context), m_module(module), m_builder(builder), m_freestanding(freestanding), m_kernel(kernel), m_jmp_buf_size(jmp_buf_size) {
}

RuntimeBuilder::~RuntimeBuilder() = default;

void RuntimeBuilder::generateRuntime() {
    generateTypes();

    if (m_freestanding) {
        generateFreestandingStubs();
        return;
    }

    if (m_kernel) {
        generateKernelRuntime();
        return;
    }

    declareCLibFunctions();

    // No GC — emit direct alloc/free + no-op stubs (generateMemoryManagement).
    generateMemoryManagement();
    generateStringOps();
    generateEquality();
    generateObjectHash();
    generateListOps();
    generateRawArrayOps();  // SIMD-1
    generateVectorOps();    // SIMD-5
    generateRecordOps();
    generateConversions();
    generateDeepClone();
    generateClosureOps();
    generateDeferOps();       // M19: must run before generateExceptionOps
    generateExceptionOps();
    generateThreadOps();
    generateMiscOps();
    generateIOOps();
    generateModuleAPIVTable();
}

Function* RuntimeBuilder::createRuntimeFunc(const std::string& name, FunctionType* type, bool variadic) {
    auto callee = m_module.getOrInsertFunction(name, type);
    auto* fn = cast<Function>(callee.getCallee());
    fn->setLinkage(Function::InternalLinkage);
    fn->setDSOLocal(true);
    return fn;
}

void RuntimeBuilder::generateTypes() {
    m_angara_obj_type = StructType::create(m_ctx, {
        Type::getInt32Ty(m_ctx),
        Type::getInt64Ty(m_ctx)
    }, "AngaraObject");

    // ObjHeader: { i32 type, i32 meta, ptr next } — created directly (no GC).
    m_obj_header_type = StructType::create(m_ctx, {
        Type::getInt32Ty(m_ctx),     // field 0: type
        Type::getInt32Ty(m_ctx),     // field 1: meta
        PointerType::get(m_ctx, 0)   // field 2: next (unused; was alloc-list/forward)
    }, "ObjHeader");

    // RtRootFrame + RtThreadState — kept for codegen compatibility (push/pop are no-ops).
    m_rt_root_frame_type = StructType::create(m_ctx, {
        PointerType::get(m_ctx, 0),  // prev_frame
        Type::getInt32Ty(m_ctx),     // count
        ArrayType::get(PointerType::get(m_ctx, 0), 1)  // slots (minimal)
    }, "RtRootFrame");

    m_rt_thread_state_type = StructType::create(m_ctx, {
        PointerType::get(m_ctx, 0),  // [0] self
        PointerType::get(m_ctx, 0),  // [1] next_thread
        PointerType::get(m_ctx, 0),  // [2] root_frames
        Type::getInt1Ty(m_ctx),      // [3] waiting
        PointerType::get(m_ctx, 0)   // [4] current_arena (unused)
    }, "RtThreadState");

    m_rt_initial_meta = ConstantInt::get(Type::getInt32Ty(m_ctx), 0x100);  // is_unique bit

    m_string_type = StructType::create(m_ctx, {
        m_obj_header_type,
        Type::getInt64Ty(m_ctx),
        Type::getInt64Ty(m_ctx),
        PointerType::get(m_ctx, 0)
    }, "AngaraString");

    m_list_type = StructType::create(m_ctx, {
        m_obj_header_type,
        Type::getInt64Ty(m_ctx),
        Type::getInt64Ty(m_ctx),
        PointerType::get(m_ctx, 0)
    }, "AngaraList");

    m_record_entry_type = StructType::create(m_ctx, {
        PointerType::get(m_ctx, 0),
        m_angara_obj_type
    }, "RecordEntry");

    m_record_type = StructType::create(m_ctx, {
        m_obj_header_type,
        Type::getInt64Ty(m_ctx),
        Type::getInt64Ty(m_ctx),
        PointerType::get(m_ctx, 0)
    }, "AngaraRecord");

    m_exception_type = StructType::create(m_ctx, {
        m_obj_header_type,
        m_angara_obj_type
    }, "AngaraException");

    auto* fn_ptr_type = PointerType::get(m_ctx, 0);
    m_closure_type = StructType::create(m_ctx, {
        m_obj_header_type,
        fn_ptr_type,
        Type::getInt32Ty(m_ctx),
        Type::getInt1Ty(m_ctx),
        PointerType::get(m_ctx, 0),   // env: pointer to captured variables array
        Type::getInt32Ty(m_ctx)       // env_count: number of captured variables (for runtime scanning)
    }, "AngaraClosure");

    m_bound_method_type = StructType::create(m_ctx, {
        m_obj_header_type,
        m_angara_obj_type,
        m_angara_obj_type
    }, "AngaraBoundMethod");

    // TS-1: a trait object — a value viewed through a trait/contract interface.
    // Carries the concrete receiver and a pointer to a per-(class,interface)
    // vtable (a ConstantArray of function pointers, one per interface method in
    // declaration order). Boxed inside a normal {TAG_OBJ, payload} AngaraObject.
    m_trait_object_type = StructType::create(m_ctx, {
        m_obj_header_type,
        m_angara_obj_type,            // receiver: the concrete instance
        PointerType::get(m_ctx, 0)    // vtable_ptr: -> [n x ptr] function pointers
    }, "AngaraTraitObject");

    // SIMD-1: unboxed dynamic array — same shape as AngaraList but the element
    // buffer is raw typed (not AngaraObject[]).  elem_size records sizeof(T) so
    // the runtime functions can GEP correctly for any element type.
    m_raw_array_type = StructType::create(m_ctx, {
        m_obj_header_type,              // field 0: header
        Type::getInt64Ty(m_ctx),        // field 1: count
        Type::getInt64Ty(m_ctx),        // field 2: capacity
        Type::getInt64Ty(m_ctx),        // field 3: elem_size (sizeof(T))
        PointerType::get(m_ctx, 0)      // field 4: ptr → raw T[] elements
    }, "AngaraRawArray");

    // SIMD-5: fixed-size SIMD vector — stores the element buffer inline via
    // overallocation. The struct has a pointer to the buffer which immediately
    // follows the header in memory.
    m_vector_type = StructType::create(m_ctx, {
        m_obj_header_type,              // field 0: header
        Type::getInt32Ty(m_ctx),        // field 1: num_elements (2, 3, 4, 8)
        Type::getInt32Ty(m_ctx),        // field 2: elem_size (sizeof(f32), etc.)
        PointerType::get(m_ctx, 0)      // field 3: ptr → raw element buffer (overallocated inline)
    }, "AngaraVector");

    m_native_instance_type = StructType::create(m_ctx, {
        m_obj_header_type,
        PointerType::get(m_ctx, 0),
        PointerType::get(m_ctx, 0),
        PointerType::get(m_ctx, 0),
    }, "AngaraNativeInstance");

    // H8: Single definition of the exception frame struct { [jmp_buf x i8], ptr }.
    // Previously created independently in four places with two different names.
    m_exc_frame_type = StructType::create(m_ctx, {
        ArrayType::get(Type::getInt8Ty(m_ctx), m_jmp_buf_size),
        PointerType::get(m_ctx, 0)
    }, "AngaraExcFrame");

    m_thread_type = StructType::create(m_ctx, {
        m_obj_header_type,
        PointerType::get(m_ctx, 0),
        m_angara_obj_type,
        Type::getInt32Ty(m_ctx),     // argc for spawned function
        PointerType::get(m_ctx, 0)   // args array (heap-allocated AngaraObject[])
    }, "AngaraThread");

    m_mutex_type = StructType::create(m_ctx, {
        m_obj_header_type,
        ArrayType::get(Type::getInt8Ty(m_ctx), 64)
    }, "AngaraMutex");

    // InternalLinkage (not CommonLinkage): the global has an explicit zero
    // initializer, so it is a real .bss symbol, never COMMON. COMMON symbols
    // are rejected by the Linux module loader ("please compile with -fno-common"),
    // which matters for the --kernel target.
    m_g_exception_chain = new GlobalVariable(
        m_module, PointerType::get(m_ctx, 0),
        false, GlobalValue::InternalLinkage,
        ConstantPointerNull::get(PointerType::get(m_ctx, 0)),
        "__ang_exception_chain");

    m_g_current_exception = new GlobalVariable(
        m_module, m_angara_obj_type,
        false, GlobalValue::InternalLinkage,
        ConstantAggregateZero::get(m_angara_obj_type),
        "__ang_current_exception");

    // Defer stack for throw_error resource cleanup (M19).
    // Fixed-size stack of {fn_ptr, arg_ptr} pairs; drained in LIFO order.
    auto* defer_entry_ty = StructType::create(m_ctx, {
        PointerType::get(m_ctx, 0),
        PointerType::get(m_ctx, 0)
    }, "DeferEntry");
    m_g_defer_stack = new GlobalVariable(
        m_module, ArrayType::get(defer_entry_ty, 16),
        false, GlobalValue::InternalLinkage,
        ConstantAggregateZero::get(ArrayType::get(defer_entry_ty, 16)),
        "__ang_defer_stack");
    m_g_defer_count = new GlobalVariable(
        m_module, Type::getInt32Ty(m_ctx),
        false, GlobalValue::InternalLinkage,
        ConstantInt::get(Type::getInt32Ty(m_ctx), 0),
        "__ang_defer_count");
}

void RuntimeBuilder::declareCLibFunctions() {
    auto* void_ty = Type::getVoidTy(m_ctx);
    auto* i8_ty = Type::getInt8Ty(m_ctx);
    auto* i32_ty = Type::getInt32Ty(m_ctx);
    auto* i64_ty = Type::getInt64Ty(m_ctx);
    auto* f64_ty = Type::getDoubleTy(m_ctx);
    auto* i8_ptr = PointerType::get(m_ctx, 0);

    m_module.getOrInsertFunction("malloc", FunctionType::get(i8_ptr, {i64_ty}, false));
    m_module.getOrInsertFunction("free", FunctionType::get(void_ty, {i8_ptr}, false));
    m_module.getOrInsertFunction("realloc", FunctionType::get(i8_ptr, {i8_ptr, i64_ty}, false));
    m_module.getOrInsertFunction("memcpy", FunctionType::get(i8_ptr, {i8_ptr, i8_ptr, i64_ty}, false));
    m_module.getOrInsertFunction("strlen", FunctionType::get(i64_ty, {i8_ptr}, false));
    m_module.getOrInsertFunction("strcmp", FunctionType::get(i32_ty, {i8_ptr, i8_ptr}, false));
    m_module.getOrInsertFunction("strdup", FunctionType::get(i8_ptr, {i8_ptr}, false));
    m_fn_printf = m_module.getOrInsertFunction("printf", FunctionType::get(i32_ty, {i8_ptr}, true));
    m_module.getOrInsertFunction("fprintf", FunctionType::get(i32_ty, {PointerType::get(m_ctx, 0), i8_ptr}, true));
    m_module.getOrInsertFunction("snprintf", FunctionType::get(i32_ty, {i8_ptr, i64_ty, i8_ptr}, true));
    m_module.getOrInsertFunction("strtoll", FunctionType::get(i64_ty, {i8_ptr, PointerType::get(m_ctx, 0), i32_ty}, false));
    m_module.getOrInsertFunction("strtod", FunctionType::get(f64_ty, {i8_ptr, PointerType::get(m_ctx, 0)}, false));
    // H18: mark setjmp as returns_twice at the declaration so every caller —
    // including the runtime's __ang_try_begin (ControlFlow.cpp) and any future
    // site — inherits the attribute. Without it, LLVM's optimizer may reorder
    // register spills/restores around the call and miscompile setjmp/longjmp.
    // The per-call addFnAttr in StmtCodegen.cpp/LLVMBackend.cpp is now
    // belt-and-suspenders (idempotent) rather than load-bearing.
    if (auto setjmp_callee = m_module.getOrInsertFunction(
            "setjmp", FunctionType::get(i32_ty, {i8_ptr}, false)).getCallee()) {
        if (auto* setjmp_fn = dyn_cast<Function>(setjmp_callee)) {
            setjmp_fn->addFnAttr(Attribute::ReturnsTwice);
        }
    }
    m_module.getOrInsertFunction("longjmp", FunctionType::get(void_ty, {i8_ptr, i32_ty}, false));
    m_module.getOrInsertFunction("exit", FunctionType::get(void_ty, {i32_ty}, false));
    m_module.getOrInsertFunction("pthread_create", FunctionType::get(i32_ty,
        {PointerType::get(m_ctx, 0), PointerType::get(m_ctx, 0),
         PointerType::get(m_ctx, 0), PointerType::get(m_ctx, 0)}, false));
    m_module.getOrInsertFunction("pthread_join", FunctionType::get(i32_ty,
        {PointerType::get(m_ctx, 0), PointerType::get(m_ctx, 0)}, false));
    m_module.getOrInsertFunction("pthread_mutex_init", FunctionType::get(i32_ty,
        {PointerType::get(m_ctx, 0), PointerType::get(m_ctx, 0)}, false));
    m_module.getOrInsertFunction("pthread_mutex_destroy", FunctionType::get(i32_ty,
        {PointerType::get(m_ctx, 0)}, false));
    m_module.getOrInsertFunction("pthread_mutex_lock", FunctionType::get(i32_ty,
        {PointerType::get(m_ctx, 0)}, false));
    m_module.getOrInsertFunction("pthread_mutex_unlock", FunctionType::get(i32_ty,
        {PointerType::get(m_ctx, 0)}, false));
    m_module.getOrInsertFunction("memset", FunctionType::get(i8_ptr, {i8_ptr, i32_ty, i64_ty}, false));

    if (m_freestanding) {
        {
            auto* fn_ty = FunctionType::get(i8_ptr, {i8_ptr, i8_ptr, i64_ty}, false);
            auto* fn = createRuntimeFunc("__ang_builtin_memcpy", fn_ty);
            auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
            IRBuilder<> b(entry);
            auto* dst = fn->arg_begin(); auto* src = fn->arg_begin()+1; auto* n = fn->arg_begin()+2;
            b.CreateMemCpy(dst, Align(1), src, Align(1), b.CreateSExt(n, i64_ty));
            b.CreateRet(dst);
        }
        {
            auto* fn_ty = FunctionType::get(i64_ty, {i8_ptr}, false);
            auto* fn = createRuntimeFunc("__ang_builtin_strlen", fn_ty);
            auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
            auto* loop_bb = BasicBlock::Create(m_ctx, "loop", fn);
            auto* body_bb = BasicBlock::Create(m_ctx, "body", fn);
            auto* done_bb = BasicBlock::Create(m_ctx, "done", fn);
            IRBuilder<> b(entry); auto* s = fn->arg_begin(); b.CreateBr(loop_bb);
            IRBuilder<> bl(loop_bb);
            auto* i_phi = bl.CreatePHI(i64_ty, 2, "i");
            i_phi->addIncoming(ConstantInt::get(i64_ty, 0), entry);
            auto* c = bl.CreateLoad(i8_ty, bl.CreateGEP(i8_ty, s, {i_phi}), "c");
            bl.CreateCondBr(bl.CreateICmpEQ(c, ConstantInt::get(i8_ty, 0)), done_bb, body_bb);
            IRBuilder<> bb(body_bb);
            auto* next = bb.CreateAdd(i_phi, ConstantInt::get(i64_ty, 1));
            bb.CreateBr(loop_bb); i_phi->addIncoming(next, body_bb);
            IRBuilder<> bd(done_bb); bd.CreateRet(i_phi);
        }
        {
            auto* fn_ty = FunctionType::get(i32_ty, {i8_ptr, i8_ptr}, false);
            auto* fn = createRuntimeFunc("__ang_builtin_strcmp", fn_ty);
            auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
            auto* loop_bb = BasicBlock::Create(m_ctx, "loop", fn);
            auto* diff_bb = BasicBlock::Create(m_ctx, "diff", fn);
            IRBuilder<> b(entry); b.CreateBr(loop_bb);
            IRBuilder<> bl(loop_bb);
            auto* i_phi = bl.CreatePHI(i64_ty, 2, "i");
            i_phi->addIncoming(ConstantInt::get(i64_ty, 0), entry);
            auto* a = fn->arg_begin(); auto* b_arg = fn->arg_begin()+1;
            auto* ca = bl.CreateLoad(i8_ty, bl.CreateGEP(i8_ty, a, {i_phi}));
            auto* cb = bl.CreateLoad(i8_ty, bl.CreateGEP(i8_ty, b_arg, {i_phi}));
            auto* both_null = bl.CreateAnd(bl.CreateICmpEQ(ca, ConstantInt::get(i8_ty, 0)), bl.CreateICmpEQ(cb, ConstantInt::get(i8_ty, 0)));
            auto* differ = bl.CreateICmpNE(ca, cb);
            auto* cont = bl.CreateAnd(bl.CreateNot(both_null), bl.CreateNot(differ));
            auto* next = bl.CreateAdd(i_phi, ConstantInt::get(i64_ty, 1));
            i_phi->addIncoming(next, loop_bb);
            bl.CreateCondBr(cont, loop_bb, diff_bb);
            IRBuilder<> bd(diff_bb);
            bd.CreateRet(bd.CreateSub(bd.CreateSExt(ca, i32_ty), bd.CreateSExt(cb, i32_ty)));
        }
        {
            auto* fn_ty = FunctionType::get(i8_ptr, {i8_ptr, i32_ty, i64_ty}, false);
            auto* fn = createRuntimeFunc("__ang_builtin_memset", fn_ty);
            auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
            IRBuilder<> b(entry);
            auto* s = fn->arg_begin(); auto* c = fn->arg_begin()+1; auto* n = fn->arg_begin()+2;
            b.CreateMemSet(s, b.CreateTrunc(c, i8_ty), b.CreateSExt(n, i64_ty), Align(1));
            b.CreateRet(s);
        }
    }
}

void RuntimeBuilder::generateDeferOps() {
    auto* void_ty = Type::getVoidTy(m_ctx);
    auto* i32_ty  = Type::getInt32Ty(m_ctx);
    auto* ptr_ty  = PointerType::get(m_ctx, 0);

    auto* defer_entry_ty = StructType::getTypeByName(m_ctx, "DeferEntry");
    auto* defer_stack_ty = ArrayType::get(defer_entry_ty, 16);

    // __ang_api_defer_push(fn_ptr, arg_ptr)
    {
        auto* ft = FunctionType::get(void_ty, {ptr_ty, ptr_ty}, false);
        auto* fn = createRuntimeFunc("__ang_api_defer_push", ft);
        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);
        auto* fn_ptr = fn->arg_begin();
        auto* arg_ptr = fn->arg_begin() + 1;

        auto* count = b.CreateLoad(i32_ty, m_g_defer_count, "count");
        auto* overflow = b.CreateICmpUGE(count, ConstantInt::get(i32_ty, 16));
        auto* overflow_bb = BasicBlock::Create(m_ctx, "overflow", fn);
        auto* push_bb = BasicBlock::Create(m_ctx, "push", fn);
        b.CreateCondBr(overflow, overflow_bb, push_bb);

        b.SetInsertPoint(overflow_bb);
        b.CreateRetVoid();

        b.SetInsertPoint(push_bb);
        auto* slot = b.CreateGEP(defer_stack_ty, m_g_defer_stack,
            {ConstantInt::get(i32_ty, 0), count});
        auto* fn_slot = b.CreateStructGEP(defer_entry_ty, slot, 0);
        b.CreateStore(fn_ptr, fn_slot);
        auto* arg_slot = b.CreateStructGEP(defer_entry_ty, slot, 1);
        b.CreateStore(arg_ptr, arg_slot);
        auto* new_count = b.CreateAdd(count, ConstantInt::get(i32_ty, 1));
        b.CreateStore(new_count, m_g_defer_count);
        b.CreateRetVoid();
    }

    // __ang_api_defer_run()
    {
        auto* ft = FunctionType::get(void_ty, {}, false);
        auto* fn = createRuntimeFunc("__ang_api_defer_run", ft);
        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<> b(entry);

        auto* loop_bb = BasicBlock::Create(m_ctx, "loop", fn);
        auto* pop_bb  = BasicBlock::Create(m_ctx, "pop", fn);
        auto* exit_bb = BasicBlock::Create(m_ctx, "exit", fn);
        b.CreateBr(loop_bb);

        b.SetInsertPoint(loop_bb);
        auto* count = b.CreateLoad(i32_ty, m_g_defer_count, "count");
        auto* done = b.CreateICmpEQ(count, ConstantInt::get(i32_ty, 0));
        b.CreateCondBr(done, exit_bb, pop_bb);

        b.SetInsertPoint(pop_bb);
        auto* idx = b.CreateSub(count, ConstantInt::get(i32_ty, 1));
        b.CreateStore(idx, m_g_defer_count);
        auto* slot = b.CreateGEP(defer_stack_ty, m_g_defer_stack,
            {ConstantInt::get(i32_ty, 0), idx});
        auto* fn_slot = b.CreateStructGEP(defer_entry_ty, slot, 0);
        auto* fn_ptr_val = b.CreateLoad(ptr_ty, fn_slot);
        auto* arg_slot = b.CreateStructGEP(defer_entry_ty, slot, 1);
        auto* arg_ptr_val = b.CreateLoad(ptr_ty, arg_slot);

        auto* call_ty = FunctionType::get(void_ty, {ptr_ty}, false);
        b.CreateCall(call_ty, fn_ptr_val, {arg_ptr_val});
        b.CreateBr(loop_bb);

        b.SetInsertPoint(exit_bb);
        b.CreateRetVoid();
    }
}

}