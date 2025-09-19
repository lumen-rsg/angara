//
// Created by cv2 on 9/19/25.
//

#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const Grouping& expr) {
        expr.expression->accept(*this);
        auto inner_type = popType();
        pushAndSave(&expr, inner_type);
        return {};
    }

}