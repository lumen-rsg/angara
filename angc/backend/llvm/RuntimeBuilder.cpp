#include "RuntimeBuilder.h"
#include "MarkSweepGC.h"
#include "ChaperoneGC.h"
#include <llvm/IR/Verifier.h>
#include <llvm/IR/Constants.h>
#include <llvm/Support/raw_ostream.h>

using namespace llvm;

namespace angara {

RuntimeBuilder::RuntimeBuilder(LLVMContext& context, Module& module, IRBuilder<>& builder, bool freestanding)
    : m_ctx(context), m_module(module), m_builder(builder), m_freestanding(freestanding) {
    // Create the GC strategy
#ifdef USE_CHAPERONE_GC
    m_gc = std::make_unique<ChaperoneGC>(m_ctx, m_module, m_builder);
#else
    m_gc = std::make_unique<MarkSweepGC>(m_ctx, m_module, m_builder);
#endif
}

RuntimeBuilder::~RuntimeBuilder() = default;

llvm::StructType* RuntimeBuilder::getGcRootFrameType() const { return m_gc->getGcRootFrameType(); }
llvm::StructType* RuntimeBuilder::getGcThreadStateType() const { return m_gc->getGcThreadStateType(); }
llvm::GlobalVariable* RuntimeBuilder::getGcThreadStateTLS() const { return m_gc->getGcThreadStateTLS(); }
llvm::FunctionCallee RuntimeBuilder::getGcPrintStatsFunc() const { return m_gc->getGcPrintStatsFunc(); }
llvm::ConstantInt* RuntimeBuilder::getGcInitialMeta() const { return m_gc->getInitialMetaConstant(); }

void RuntimeBuilder::generateRuntime() {
    generateTypes();

    if (m_freestanding) {
        generateFreestandingStubs();
        return;
    }

    declareCLibFunctions();

    // GC: generate globals and functions
    m_gc->generateGlobals();
    m_gc->generateFunctions();

    generateMemoryManagement();
    generateStringOps();
    generateEquality();
    generateListOps();
    generateRecordOps();
    generateConversions();
    generateDeepClone();
    generateClosureOps();
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

    // Let the GC create its header type
    m_gc->generateTypes();
    m_obj_header_type = m_gc->getHeaderType();

    // Give the GC access to the AngaraObject type for function signatures
    m_gc->setAngaraObjType(m_angara_obj_type);

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
        Type::getInt32Ty(m_ctx)       // env_count: number of captured variables (for GC scanning)
    }, "AngaraClosure");

    m_bound_method_type = StructType::create(m_ctx, {
        m_obj_header_type,
        m_angara_obj_type,
        m_angara_obj_type
    }, "AngaraBoundMethod");

    m_native_instance_type = StructType::create(m_ctx, {
        m_obj_header_type,
        PointerType::get(m_ctx, 0),
        PointerType::get(m_ctx, 0),
        PointerType::get(m_ctx, 0),
    }, "AngaraNativeInstance");

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

    // Give the GC access to all runtime struct types for scanner traversal
    m_gc->setStructTypes(
        m_string_type, m_list_type, m_record_type, m_record_entry_type,
        m_exception_type, m_closure_type, m_bound_method_type,
        m_thread_type, m_native_instance_type);

    m_g_exception_chain = new GlobalVariable(
        m_module, PointerType::get(m_ctx, 0),
        false, GlobalValue::CommonLinkage,
        ConstantPointerNull::get(PointerType::get(m_ctx, 0)),
        "__ang_exception_chain");

    m_g_current_exception = new GlobalVariable(
        m_module, m_angara_obj_type,
        false, GlobalValue::InternalLinkage,
        ConstantAggregateZero::get(m_angara_obj_type),
        "__ang_current_exception");
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
    m_module.getOrInsertFunction("setjmp", FunctionType::get(i32_ty, {i8_ptr}, false));
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

}