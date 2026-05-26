#include "TypeChecker.h"
namespace angara {

    void TypeChecker::visit(std::shared_ptr<const WhileStmt> stmt) {
        stmt->condition->accept(*this);
        auto condition_type = popType();

        if (!isTruthy(condition_type)) {
            error(Token(), "While loop condition must be a truthy type (bool or number), but got '" +
                           condition_type->toString() + "'.");
        }

        m_loop_depth++;
        stmt->body->accept(*this, stmt->body);
        m_loop_depth--;
    }

}
