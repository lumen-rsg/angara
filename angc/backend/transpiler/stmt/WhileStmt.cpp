//
// Created by cv2 on 9/19/25.
//
#include "CTranspiler.h"
namespace angara {

    void CTranspiler::transpileWhileStmt(const WhileStmt& stmt) {
        std::string condition_str = "angara_is_truthy(" + transpileExpr(stmt.condition) + ")";

        indent();
        (*m_current_out) << "while (" << condition_str << ") ";

        // Transpile the body of the loop.
        transpileStmt(stmt.body);
    }

}