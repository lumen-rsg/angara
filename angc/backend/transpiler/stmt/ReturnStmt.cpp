//
// Created by cv2 on 9/19/25.
//
#include "CTranspiler.h"
namespace angara {

    void CTranspiler::transpileReturnStmt(const ReturnStmt& stmt) {
        indent();
        (*m_current_out) << "return";
        if (stmt.value) {
            (*m_current_out) << " " << transpileExpr(stmt.value);
        }
        (*m_current_out) << ";\n";
    }

}