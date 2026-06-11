#include "RuntimeBuilder.h"

using namespace llvm;

namespace angara {

void RuntimeBuilder::generateMemoryManagement() {
    auto* void_ty = Type::getVoidTy(m_ctx);
    auto* obj_ty = m_angara_obj_type;

    // During the transition from ARC to GC, incref/decref are kept as no-ops.
    // They will be removed entirely once all call sites are cleaned up in Stage 2/3.

    {
        auto* fn_ty = FunctionType::get(void_ty, {obj_ty}, false);
        auto callee = m_module.getOrInsertFunction("__ang_incref", fn_ty);
        auto* fn = cast<Function>(callee.getCallee());
        fn->setLinkage(Function::InternalLinkage);
        fn->setDSOLocal(true);
        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<>(entry).CreateRetVoid();
    }

    {
        auto* fn_ty = FunctionType::get(void_ty, {obj_ty}, false);
        auto callee = m_module.getOrInsertFunction("__ang_decref", fn_ty);
        auto* fn = cast<Function>(callee.getCallee());
        fn->setLinkage(Function::InternalLinkage);
        fn->setDSOLocal(true);
        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<>(entry).CreateRetVoid();
    }

    // __ang_free_object is still needed during the transition for any code that
    // calls it directly. In the GC world, finalization happens during sweep.
    // Keep it as a no-op for now — the GC sweep handles freeing.
    {
        auto* fn_ty = FunctionType::get(void_ty, {PointerType::get(m_ctx, 0)}, false);
        auto callee = m_module.getOrInsertFunction("__ang_free_object", fn_ty);
        auto* fn = cast<Function>(callee.getCallee());
        fn->setLinkage(Function::InternalLinkage);
        fn->setDSOLocal(true);
        auto* entry = BasicBlock::Create(m_ctx, "entry", fn);
        IRBuilder<>(entry).CreateRetVoid();
    }
}

} // namespace angara
