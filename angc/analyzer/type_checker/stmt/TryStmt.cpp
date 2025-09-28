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

        // --- THIS IS THE FIX ---
        // Determine the type of the exception variable.
        std::shared_ptr<Type> exception_var_type;
        if (stmt->catchType) {
            // If the user provided a type, resolve and use it.
            exception_var_type = resolveType(stmt->catchType);
        } else {
            // If no type was provided, it defaults to `any`.
            exception_var_type = m_type_any;
        }
        // --- END FIX ---

        // 3. Declare the exception variable (e.g., 'e') with the correct type.
        if (auto conflicting_symbol = m_symbols.declare(stmt->catchName, exception_var_type, true)) { // `e` is a constant
            error(stmt->catchName, "re-declaration of variable '" + stmt->catchName.lexeme + "'.");
            note(conflicting_symbol->declaration_token, "previous declaration was here.");
        }

        // 4. With the correctly typed exception variable in scope, type check the 'catch' block.
        stmt->catchBlock->accept(*this, stmt->catchBlock);

        // 5. Exit the scope for the catch block.
        m_symbols.exitScope();
    }

}