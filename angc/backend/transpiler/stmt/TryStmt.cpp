//
// Created by cv2 on 9/19/25.
//
#include "CTranspiler.h"
namespace angara {

    void CTranspiler::transpileTryStmt(const TryStmt& stmt) {
        indent(); (*m_current_out) << "{\n";
        m_indent_level++;
        indent(); (*m_current_out) << "ExceptionFrame __frame;\n";
        indent(); (*m_current_out) << "__frame.prev = g_exception_chain_head;\n";
        indent(); (*m_current_out) << "g_exception_chain_head = &__frame;\n";

        indent(); (*m_current_out) << "if (setjmp(__frame.buffer) == 0) {\n";
        m_indent_level++;

        transpileStmt(stmt.tryBlock);

        m_indent_level--;
        indent(); (*m_current_out) << "}\n";

        indent(); (*m_current_out) << "g_exception_chain_head = __frame.prev; // Pop the frame\n";

        indent(); (*m_current_out) << "if (g_current_exception.type != VAL_NIL) {\n";
        m_indent_level++;

        indent(); (*m_current_out) << "AngaraObject " << stmt.catchName.lexeme << " = g_current_exception;\n";
        indent(); (*m_current_out) << "g_current_exception = angara_create_nil();\n";

        transpileStmt(stmt.catchBlock);

        m_indent_level--;
        indent(); (*m_current_out) << "}\n";
        m_indent_level--;
        indent(); (*m_current_out) << "}\n";
    }

}