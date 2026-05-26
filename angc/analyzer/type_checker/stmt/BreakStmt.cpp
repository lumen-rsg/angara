#include "TypeChecker.h"
namespace angara {

    void TypeChecker::visit(std::shared_ptr<const BreakStmt> stmt) {
        if (m_loop_depth == 0) {
            error(stmt->keyword, "'break' can only be used inside a loop.");
        }
    }

    void TypeChecker::visit(std::shared_ptr<const ContinueStmt> stmt) {
        if (m_loop_depth == 0) {
            error(stmt->keyword, "'continue' can only be used inside a loop.");
        }
    }

}
