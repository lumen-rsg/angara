//
// Created by cv2 on 9/19/25.
//

#include "CTranspiler.h"
namespace angara{

    void CTranspiler::transpileThrowStmt(const ThrowStmt& stmt) {
        indent();
        (*m_current_out) << "angara_throw(" << transpileExpr(stmt.expression) << ");\n";
    }

}