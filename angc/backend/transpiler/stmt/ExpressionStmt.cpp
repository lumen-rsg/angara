//
// Created by cv2 on 9/19/25.
//
#include "CTranspiler.h"
namespace angara {

    void CTranspiler::transpileExpressionStmt(const ExpressionStmt& stmt) {
        indent();
        (*m_current_out) << transpileExpr(stmt.expression) << ";\n";
    }

}
