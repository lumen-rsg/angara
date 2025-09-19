//
// Created by cv2 on 9/19/25.
//

#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const IsExpr& expr) {
        // The `is` operator itself doesn't need narrowing; it just produces a boolean.
        // We visit the sub-expressions to ensure they are valid.
        expr.object->accept(*this);
        popType(); // We don't need the object's type here.

        // We don't need to "visit" the RHS type, just resolve it.
        resolveType(expr.type);

        // The result of an `is` expression is always a boolean.
        pushAndSave(&expr, m_type_bool);
        return {};
    }

}