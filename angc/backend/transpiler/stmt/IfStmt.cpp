//
// Created by cv2 on 9/19/25.
//
#include "CTranspiler.h"
namespace angara {

    void CTranspiler::transpileIfStmt(const IfStmt& stmt) {
        // --- Case 1: Handle `if let` for optional unwrapping ---
        if (stmt.declaration) {
            // We generate a new C scope to contain the temporary variable.
            indent(); (*m_current_out) << "{\n";
            m_indent_level++;

            // 1. Evaluate the initializer into a temporary variable.
            indent();
            (*m_current_out) << "AngaraObject __tmp_if_let = " << transpileExpr(stmt.declaration->initializer) << ";\n";

            // 2. The condition is a simple nil check on the temporary.
            indent();
            (*m_current_out) << "if (!IS_NIL(__tmp_if_let)) {\n";
            m_indent_level++;

            // 3. If not nil, declare the new variable inside the `if` block.
            indent();
            (*m_current_out) << "const AngaraObject " << sanitize_name(stmt.declaration->name.lexeme)
                             << " = __tmp_if_let;\n";

            // 4. Transpile the 'then' block.
            transpileStmt(stmt.thenBranch);

            m_indent_level--;
            indent(); (*m_current_out) << "}";

            // 5. Transpile the 'else' block, if it exists.
            if (stmt.elseBranch) {
                (*m_current_out) << " else ";
                transpileStmt(stmt.elseBranch);
            } else {
                (*m_current_out) << "\n";
            }

            m_indent_level--;
            indent(); (*m_current_out) << "}\n";

            return; // We have handled the entire `if let` statement.
        }

        // --- Case 2: Handle regular `if` with a boolean condition ---
        std::string condition_str = "angara_is_truthy(" + transpileExpr(stmt.condition) + ")";
        indent();
        (*m_current_out) << "if (" << condition_str << ") ";
        transpileStmt(stmt.thenBranch);

        if (stmt.elseBranch) {
            indent(); // Indent for the 'else' keyword
            (*m_current_out) << "else ";
            transpileStmt(stmt.elseBranch);
        }
    }

}