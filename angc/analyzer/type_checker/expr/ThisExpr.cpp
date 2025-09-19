//
// Created by cv2 on 9/19/25.
//
#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const ThisExpr& expr) {
        // --- RULE 1: Check if we are inside a class ---
        if (m_current_class == nullptr) {
            error(expr.keyword, "Cannot use 'this' outside of a class method.");
            pushAndSave(&expr, m_type_error);
            return {};
        }

        // --- RULE 2: The type of 'this' is the instance type of the current class ---
        pushAndSave(&expr, std::make_shared<InstanceType>(m_current_class));
        return {};
    }

}
