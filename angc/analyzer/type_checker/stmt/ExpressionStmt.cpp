//
// Created by cv2 on 9/19/25.
//

#include "TypeChecker.h"
namespace angara {

    void TypeChecker::visit(std::shared_ptr<const ExpressionStmt> stmt) {
        // 1. Recursively type check the inner expression.
        stmt->expression->accept(*this);

        // 2. The type of that expression is now on our stack. Since the value is
        //    not used, we pop its type to keep our analysis stack clean.
        popType();
    }


}