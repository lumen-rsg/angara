//
// Created by cv2 on 9/19/25.
//

#include "TypeChecker.h"
namespace angara {

    void TypeChecker::visit(std::shared_ptr<const ForStmt> stmt) {
        // 1. A C-style for loop introduces a new scope for its initializer.
        m_symbols.enterScope();

        // 2. Type check the initializer, if it exists.
        if (stmt->initializer) {
            stmt->initializer->accept(*this, stmt->initializer);
        }

        // 3. Type check the condition, if it exists.
        if (stmt->condition) {
            stmt->condition->accept(*this);
            auto condition_type = popType();
            // Enforce that the condition is a "truthy" type.
            if (!isTruthy(condition_type)) {
                error(stmt->keyword, "For loop condition must be a truthy type (bool or number), but got '" +
                                     condition_type->toString() + "'.");
            }
        }

        // 4. Type check the increment, if it exists.
        if (stmt->increment) {
            stmt->increment->accept(*this);
            popType(); // The resulting value of the increment expression is not used.
        }

        m_loop_depth++; // <-- ENTER a loop
        stmt->body->accept(*this, stmt->body);
        m_loop_depth--; // <-- EXIT a loop

        // 6. Exit the scope, destroying the initializer variable.
        m_symbols.exitScope();
    }

}