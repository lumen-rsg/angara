//
// Created by cv2 on 9/19/25.
//

#include "TypeChecker.h"
namespace angara {

    void TypeChecker::visit(std::shared_ptr<const WhileStmt> stmt) {
        // 1. Type check the condition expression.
        stmt->condition->accept(*this);
        auto condition_type = popType();

        // 2. Enforce the rule: the condition must be a 'bool'.
        if (!isTruthy(condition_type)) {
            error(Token(), "While loop condition must be of type 'bool', but got '" +
                           condition_type->toString() + "'.");
        }

        // 3. Type check the loop body.
        m_loop_depth++; // <-- ENTER a loop
        stmt->body->accept(*this, stmt->body);
        m_loop_depth--; // <-- EXIT a loop
    }

}