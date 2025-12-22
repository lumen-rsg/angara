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
        } else {
            // FIX: Explicitly return the nil object for bare returns.
            // This satisfies the C signature 'AngaraObject func(...)'.
            (*m_current_out) << " angara_create_nil()";
        }
        (*m_current_out) << ";\n";
    }

}