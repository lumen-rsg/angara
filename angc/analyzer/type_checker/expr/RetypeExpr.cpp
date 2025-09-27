//
// Created by cv2 on 27.09.2025.
//

#include "TypeChecker.h"

namespace angara {

    std::any TypeChecker::visit(const RetypeExpr& expr) {
        // 1. Resolve the target type the user specified.
        auto target_type = resolveType(expr.target_type);

        // 2. Type-check the inner expression.
        expr.expression->accept(*this);
        auto source_type = popType();

        // 3. Validate the rules.
        if (target_type->kind == TypeKind::ERROR || source_type->kind == TypeKind::ERROR) {
            pushAndSave(&expr, m_type_error);
            return {};
        }

        // Rule 1: The source expression must be a c_ptr.
        if (source_type->kind != TypeKind::C_PTR) {
            error(expr.keyword, "The 'retype' operator can only be used on an expression of type 'c_ptr', but got '" + source_type->toString() + "'.");
        }

        // Rule 2: The target type must be a foreign data type.
        if (target_type->kind != TypeKind::DATA || !std::dynamic_pointer_cast<DataType>(target_type)->is_foreign) {
            error(expr.keyword, "The target of a 'retype' must be a 'foreign data' type, but got '" + target_type->toString() + "'.");
        }

        // 4. Success! The type of the entire retype<T>(expr) expression is T.
        // This is what allows the subsequent `.sysname` access to work.
        pushAndSave(&expr, target_type);

        return {};
    }

} // namespace angara