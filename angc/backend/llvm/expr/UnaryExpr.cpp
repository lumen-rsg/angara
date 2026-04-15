#include "LLVMBackend.h"

namespace angara {

llvm::Value* LLVMBackend::codegenUnary(const Unary& expr) {
    llvm::Value* right = codegenExpr(expr.right);
    auto right_type = m_type_checker.m_expression_types.at(expr.right.get());

    if (expr.op.type == TokenType::MINUS) {
        if (isFloat(right_type)) {
            llvm::Value* v = extractF64(right);
            return callRuntimeFunc("angara_create_f64",
                {m_builder->CreateFNeg(v, "fneg")});
        }
        llvm::Value* v = extractI64(right);
        return callRuntimeFunc("angara_create_i64",
            {m_builder->CreateNeg(v, "ineg")});
    }
    if (expr.op.type == TokenType::BANG) {
        llvm::Value* b = isTruthy(right);
        b = m_builder->CreateNot(b, "lnot");
        return callRuntimeFunc("angara_create_bool", {b});
    }
    return createAngaraNil();
}

llvm::Value* LLVMBackend::codegenLogicalExpr(const LogicalExpr& expr) {
    llvm::Value* lhs = codegenExpr(expr.left);
    auto* fn = m_builder->GetInsertBlock()->getParent();
    auto& ctx = *m_context;

    llvm::BasicBlock* rhs_bb = llvm::BasicBlock::Create(ctx, "log.rhs", fn);
    llvm::BasicBlock* merge_bb = llvm::BasicBlock::Create(ctx, "log.merge");
    llvm::BasicBlock* truthy_bb = llvm::BasicBlock::Create(ctx, "log.true");

    llvm::Value* truthy = isTruthy(lhs);
    if (expr.op.type == TokenType::LOGICAL_AND) {
        m_builder->CreateCondBr(truthy, rhs_bb, merge_bb);
    } else {
        m_builder->CreateCondBr(truthy, truthy_bb, rhs_bb);
    }

    // Truthy path for OR - just return lhs
    fn->insert(fn->end(), truthy_bb);
    m_builder->SetInsertPoint(truthy_bb);
    auto* true_alloca = createAlloca(fn, "log.val");
    m_builder->CreateStore(lhs, true_alloca);
    m_builder->CreateBr(merge_bb);

    // RHS block
    m_builder->SetInsertPoint(rhs_bb);
    llvm::Value* rhs = codegenExpr(expr.right);
    auto* rhs_alloca = createAlloca(fn, "log.rhs.val");
    m_builder->CreateStore(rhs, rhs_alloca);
    m_builder->CreateBr(merge_bb);

    // Merge
    fn->insert(fn->end(), merge_bb);
    m_builder->SetInsertPoint(merge_bb);
    auto* phi = m_builder->CreatePHI(m_angara_obj_type, 2, "log.result");
    phi->addIncoming(lhs, true_alloca->getParent());
    phi->addIncoming(rhs, rhs_bb);

    // Load from appropriate alloca via phi
    return phi;
}

llvm::Value* LLVMBackend::codegenTernaryExpr(const TernaryExpr& expr) {
    llvm::Value* cond = codegenExpr(expr.condition);
    auto* fn = m_builder->GetInsertBlock()->getParent();
    auto& ctx = *m_context;

    llvm::BasicBlock* then_bb = llvm::BasicBlock::Create(ctx, "tern.then", fn);
    llvm::BasicBlock* else_bb = llvm::BasicBlock::Create(ctx, "tern.else");
    llvm::BasicBlock* merge_bb = llvm::BasicBlock::Create(ctx, "tern.merge");

    llvm::Value* truthy = isTruthy(cond);
    m_builder->CreateCondBr(truthy, then_bb, else_bb);

    m_builder->SetInsertPoint(then_bb);
    llvm::Value* then_val = codegenExpr(expr.thenBranch);
    auto* then_alloca = createAlloca(fn, "tern.then.val");
    m_builder->CreateStore(then_val, then_alloca);
    m_builder->CreateBr(merge_bb);

    fn->insert(fn->end(), else_bb);
    m_builder->SetInsertPoint(else_bb);
    llvm::Value* else_val = codegenExpr(expr.elseBranch);
    auto* else_alloca = createAlloca(fn, "tern.else.val");
    m_builder->CreateStore(else_val, else_alloca);
    m_builder->CreateBr(merge_bb);

    fn->insert(fn->end(), merge_bb);
    m_builder->SetInsertPoint(merge_bb);
    auto* phi = m_builder->CreatePHI(m_angara_obj_type, 2, "tern.result");
    phi->addIncoming(then_val, then_bb);
    phi->addIncoming(else_val, else_bb);
    return phi;
}

} // namespace angara