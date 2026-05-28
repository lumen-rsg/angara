#include "TypeChecker.h"
namespace angara {

    void TypeChecker::visit(std::shared_ptr<const ForStmt> stmt) {
        m_symbols.enterScope();

        if (stmt->initializer) {
            stmt->initializer->accept(*this, stmt->initializer);
        }

        if (stmt->condition) {
            stmt->condition->accept(*this);
            auto condition_type = popType();
            if (!isTruthy(condition_type)) {
                error(stmt->keyword, "For loop condition must be a truthy type (bool or number), but got '" +
                                     condition_type->toString() + "'.", "E259");
            }
        }

        if (stmt->increment) {
            stmt->increment->accept(*this);
            popType();
        }

        m_loop_depth++;
        stmt->body->accept(*this, stmt->body);
        m_loop_depth--;

        exitScopeAndWarn();
    }

}
