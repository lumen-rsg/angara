#include "LLVMBackend.h"

namespace angara {

void LLVMBackend::codegenStmt(const std::shared_ptr<Stmt>& stmt) {
    if (auto s = std::dynamic_pointer_cast<const VarDeclStmt>(stmt))
        return codegenVarDecl(*s);
    if (auto s = std::dynamic_pointer_cast<const ExpressionStmt>(stmt))
        return codegenExpressionStmt(*s);
    if (auto s = std::dynamic_pointer_cast<const BlockStmt>(stmt))
        return codegenBlock(*s);
    if (auto s = std::dynamic_pointer_cast<const IfStmt>(stmt))
        return codegenIfStmt(*s);
    if (auto s = std::dynamic_pointer_cast<const WhileStmt>(stmt))
        return codegenWhileStmt(*s);
    if (auto s = std::dynamic_pointer_cast<const ForStmt>(stmt))
        return codegenForStmt(*s);
    if (auto s = std::dynamic_pointer_cast<const ForInStmt>(stmt))
        return codegenForInStmt(*s);
    if (auto s = std::dynamic_pointer_cast<const ReturnStmt>(stmt))
        return codegenReturnStmt(*s);
    if (auto s = std::dynamic_pointer_cast<const BreakStmt>(stmt))
        return codegenBreakStmt(*s);
    if (auto s = std::dynamic_pointer_cast<const ThrowStmt>(stmt))
        return codegenThrowStmt(*s);
    if (auto s = std::dynamic_pointer_cast<const TryStmt>(stmt))
        return codegenTryStmt(*s);
    if (auto s = std::dynamic_pointer_cast<const UnsafeBlockStmt>(stmt))
        return codegenUnsafeBlockStmt(*s);
}

void LLVMBackend::codegenVarDecl(const VarDeclStmt& stmt) {
    const std::string name = sanitizeName(stmt.name.lexeme);
    llvm::Value* init_val = stmt.initializer ? codegenExpr(stmt.initializer) : createAngaraNil();
    if (!m_builder->GetInsertBlock()) return;
    auto* fn = m_builder->GetInsertBlock()->getParent();
    auto* alloca = createAlloca(fn, name);
    m_builder->CreateStore(init_val, alloca);
    m_named_values[name] = alloca;
}

void LLVMBackend::codegenExpressionStmt(const ExpressionStmt& stmt) {
    codegenExpr(stmt.expression);
}

void LLVMBackend::codegenBlock(const BlockStmt& stmt) {
    auto saved_values = m_named_values;
    for (const auto& s : stmt.statements)
        codegenStmt(s);
    m_named_values = saved_values;
}

void LLVMBackend::codegenReturnStmt(const ReturnStmt& stmt) {
    llvm::Value* val = stmt.value ? codegenExpr(stmt.value) : createAngaraNil();
    m_builder->CreateRet(val);
}

void LLVMBackend::codegenBreakStmt(const BreakStmt&) {
    if (m_loop_exit_block)
        m_builder->CreateBr(m_loop_exit_block);
}

} // namespace angara