//
// Created by cv2 on 9/19/25.
//

#include "TypeChecker.h"
namespace angara {

    void TypeChecker::visit(std::shared_ptr<const TryStmt> stmt) {
        // 1. Type check the 'try' block.
        stmt->tryBlock->accept(*this, stmt->tryBlock);

        // 2. Now, handle the 'catch' block. It introduces a new scope.
        m_symbols.enterScope();

        // 3. Declare the exception variable (e.g., 'e') in this new scope.
        if (auto conflicting_symbol = m_symbols.declare(stmt->catchName, m_type_any, false)) {
            // This is technically unreachable if the parser works correctly, but it's good practice.
            error(stmt->catchName, "re-declaration of variable '" + stmt->catchName.lexeme + "'.");
            note(conflicting_symbol->declaration_token, "previous declaration was here.");
        }

        // 4. With the exception variable in scope, type check the 'catch' block.
        stmt->catchBlock->accept(*this, stmt->catchBlock);

        // Exit the scope for the catch block, destroying the exception variable 'e'.
        m_symbols.exitScope();
    }

}