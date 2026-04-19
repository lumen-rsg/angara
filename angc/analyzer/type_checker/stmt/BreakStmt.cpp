//
// Created by cv2 on 9/19/25.
//
#include "TypeChecker.h"
namespace angara {

    void TypeChecker::visit(std::shared_ptr<const BreakStmt> stmt) {
        if (m_loop_depth == 0) {
            error(stmt->keyword, "Cannot use 'break' outside of a loop.");
        }
    }

    void TypeChecker::visit(std::shared_ptr<const ContinueStmt> stmt) {
        if (m_loop_depth == 0) {
            error(stmt->keyword, "Cannot use 'continue' outside of a loop.");
        }
    }

}
