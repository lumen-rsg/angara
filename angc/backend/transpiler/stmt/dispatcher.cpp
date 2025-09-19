//
// Created by cv2 on 9/19/25.
//
#include "CTranspiler.h"
namespace angara {

    void CTranspiler::transpileStmt(const std::shared_ptr<Stmt>& stmt) {
        if (auto var_decl = std::dynamic_pointer_cast<const VarDeclStmt>(stmt)) {
            transpileVarDecl(*var_decl);
        } else if (auto expr_stmt = std::dynamic_pointer_cast<const ExpressionStmt>(stmt)) {
            transpileExpressionStmt(*expr_stmt);
        } else if (auto block = std::dynamic_pointer_cast<const BlockStmt>(stmt)) {
            transpileBlock(*block);
        } else if (auto if_stmt = std::dynamic_pointer_cast<const IfStmt>(stmt)) {
            transpileIfStmt(*if_stmt);
        } else if (auto while_stmt = std::dynamic_pointer_cast<const WhileStmt>(stmt)) {
            transpileWhileStmt(*while_stmt);
        } else if (auto for_stmt = std::dynamic_pointer_cast<const ForStmt>(stmt)) {
            transpileForStmt(*for_stmt);
        } else if (auto ret_stmt = std::dynamic_pointer_cast<const ReturnStmt>(stmt)) {
            transpileReturnStmt(*ret_stmt);
        } else if (auto try_stmt = std::dynamic_pointer_cast<const TryStmt>(stmt)) {
            transpileTryStmt(*try_stmt);
        } else if (auto throw_stmt = std::dynamic_pointer_cast<const ThrowStmt>(stmt)) {
            transpileThrowStmt(*throw_stmt);
        } else if (auto for_in_stmt = std::dynamic_pointer_cast<const ForInStmt>(stmt)) {
            transpileForInStmt(*for_in_stmt);
        } else if (auto break_stmt = std::dynamic_pointer_cast<const BreakStmt>(stmt)) {
            transpileBreakStmt(*break_stmt);
        }
        else {
            indent();
            (*m_current_out) << "/* unhandled statement */;\n";
        }
    }

}