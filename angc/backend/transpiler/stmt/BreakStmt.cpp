//
// Created by cv2 on 9/19/25.
//
#include "CTranspiler.h"
namespace angara {

    void CTranspiler::transpileBreakStmt(const BreakStmt& stmt) {
        indent();
        (*m_current_out) << "break;\n";
    }

}