//
// Created by cv2 on 9/19/25.
//
#include "CTranspiler.h"
namespace angara {

    void CTranspiler::transpileBlock(const BlockStmt& stmt) {
        indent(); (*m_current_out) << "{\n";
        m_indent_level++;
        for (const auto& s : stmt.statements) {
            transpileStmt(s);
        }
        m_indent_level--;
        indent(); (*m_current_out) << "}\n";
    }

}