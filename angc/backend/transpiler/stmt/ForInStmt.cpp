//
// Created by cv2 on 9/19/25.
//
#include "CTranspiler.h"
namespace angara {

    void CTranspiler::transpileForInStmt(const ForInStmt& stmt) {
        indent(); (*m_current_out) << "{\n"; // Start a new scope
        m_indent_level++;

        // 1. Create and initialize the hidden __collection variable.
        indent();
        (*m_current_out) << "AngaraObject __collection_" << sanitize_name(stmt.name.lexeme)  << " = "
                         << transpileExpr(stmt.collection) << ";\n";
        indent();
        (*m_current_out) << "angara_incref(__collection_" << sanitize_name(stmt.name.lexeme)  << ");\n";

        // 2. Create and initialize the hidden __index variable.
        indent();
        (*m_current_out) << "AngaraObject __index_" << sanitize_name(stmt.name.lexeme)  << " = angara_create_i64(0LL);\n";

        // 3. Generate the `while` loop header.
        indent();
        (*m_current_out) << "while (angara_is_truthy(angara_create_bool(AS_I64(__index_" << sanitize_name(stmt.name.lexeme)  << ") < AS_I64(angara_len(__collection_" << sanitize_name(stmt.name.lexeme)  << "))))) {\n";
        m_indent_level++;

        // 4. Generate the `let item = ...` declaration.
        indent();
        (*m_current_out) << "AngaraObject " << sanitize_name(stmt.name.lexeme)  << " = angara_list_get(__collection_"
                         << sanitize_name(stmt.name.lexeme)  << ", __index_" << sanitize_name(stmt.name.lexeme)  << ");\n";

        // 5. Transpile the user's loop body.
        transpileStmt(stmt.body);

        // 6. Generate the increment logic: `__index = __index + 1`.
        indent();
        (*m_current_out) << "{\n";
        m_indent_level++;
        indent(); (*m_current_out) << "AngaraObject __temp_one = angara_create_i64(1LL);\n";
        indent(); (*m_current_out) << "AngaraObject __new_index = angara_create_i64(AS_I64(__index_" << sanitize_name(stmt.name.lexeme)  << ") + AS_I64(__temp_one));\n";
        indent(); (*m_current_out) << "angara_decref(__index_" << sanitize_name(stmt.name.lexeme)  << ");\n";
        indent(); (*m_current_out) << "__index_" << sanitize_name(stmt.name.lexeme)  << " = __new_index;\n";
        m_indent_level--;
        indent(); (*m_current_out) << "}\n";

        // 7. Decref the user's loop variable at the end of the iteration.
        indent(); (*m_current_out) << "angara_decref(" << sanitize_name(stmt.name.lexeme)  << ");\n";

        m_indent_level--;
        indent();
        (*m_current_out) << "}\n";

        // 8. Decref the hidden variables at the end of the scope.
        indent();
        (*m_current_out) << "angara_decref(__collection_" << sanitize_name(stmt.name.lexeme) << ");\n";
        indent();
        (*m_current_out) << "angara_decref(__index_" << sanitize_name(stmt.name.lexeme)  << ");\n";

        m_indent_level--;
        indent(); (*m_current_out) << "}\n";
    }

}