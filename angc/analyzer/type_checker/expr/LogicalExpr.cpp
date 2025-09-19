//
// Created by cv2 on 9/19/25.
//

#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const LogicalExpr& expr) {
        // --- Case 1: Handle the Nil Coalescing Operator `??` ---
        if (expr.op.type == TokenType::QUESTION_QUESTION) {
            // 1a. Type check the left-hand side (the optional value).
            expr.left->accept(*this);
            auto lhs_type = popType();

            // 1b. Type check the right-hand side (the default value).
            expr.right->accept(*this);
            auto rhs_type = popType();

            // Bail out early on sub-expression errors.
            if (lhs_type->kind == TypeKind::ERROR || rhs_type->kind == TypeKind::ERROR) {
                pushAndSave(&expr, m_type_error);
                return {};
            }

            // 1c. The LHS must be an optional type.
            if (lhs_type->kind != TypeKind::OPTIONAL) {
                error(expr.op, "The left-hand side of the '??' operator must be an optional type (e.g., 'string?'), but got a non-optional type '" + lhs_type->toString() + "'.");
                pushAndSave(&expr, m_type_error);
                return {};
            }

            auto unwrapped_lhs_type = std::dynamic_pointer_cast<OptionalType>(lhs_type)->wrapped_type;

            // 1d. The RHS (default value) must be compatible with the unwrapped type.
            if (!check_type_compatibility(unwrapped_lhs_type, rhs_type)) {
                error(expr.op, "Type mismatch in '??' operator. The default value of type '" + rhs_type->toString() +
                               "' is not compatible with the expected unwrapped type '" + unwrapped_lhs_type->toString() + "'.");
                pushAndSave(&expr, m_type_error);
                return {};
            }

            // 1e. The result of the `??` expression is the non-optional, unwrapped type.
            pushAndSave(&expr, unwrapped_lhs_type);
            return {};
        }

        // --- Case 2: Handle Logical AND (`&&`) and OR (`||`) ---
        // (This logic is now the fallback case)
        expr.left->accept(*this);
        auto left_type = popType();
        expr.right->accept(*this);
        auto right_type = popType();

        if (left_type->kind == TypeKind::ERROR || right_type->kind == TypeKind::ERROR) {
            pushAndSave(&expr, m_type_error);
            return {};
        }

        // Rule: Both operands must be "truthy" (convertible to a boolean).
        // Our isTruthy() check is very permissive, which is fine.
        if (!isTruthy(left_type) || !isTruthy(right_type)) {
            error(expr.op, "Operands for a logical operator ('&&', '||') must be truthy types. "
                           "Got '" + left_type->toString() + "' and '" + right_type->toString() + "'.");
            pushAndSave(&expr, m_type_error);
            return {};
        }

        // The result of a logical '&&' or '||' expression is always a boolean.
        pushAndSave(&expr, m_type_bool);
        return {};
    }

}