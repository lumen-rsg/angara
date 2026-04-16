//
// Created by cv2 on 9/19/25.
//

#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const LogicalExpr& expr) {
        // --- Case 1: Handle the Nil Coalescing Operator `??` ---
        if (expr.op.type == TokenType::QUESTION_QUESTION) {
            // --- THIS IS THE FIX for the `any ?? default` case ---
            expr.left->accept(*this);
            auto lhs_type = popType();
            expr.right->accept(*this);
            auto rhs_type = popType();

            if (lhs_type->kind == TypeKind::ERROR || rhs_type->kind == TypeKind::ERROR) {
                pushAndSave(&expr, m_type_error);
                return {};
            }

            // Rule 1 (Existing): The LHS is an optional type.
            if (lhs_type->kind == TypeKind::OPTIONAL) {
                auto unwrapped_lhs_type = std::dynamic_pointer_cast<OptionalType>(lhs_type)->wrapped_type;
                if (!check_type_compatibility(unwrapped_lhs_type, rhs_type)) {
                    error(expr.op, "Type mismatch in '??' operator. The default value of type '" + rhs_type->toString() +
                                   "' is not compatible with the expected unwrapped type '" + unwrapped_lhs_type->toString() + "'.");
                    pushAndSave(&expr, m_type_error);
                    return {};
                }
                pushAndSave(&expr, unwrapped_lhs_type);
                return {};
            }

            // Rule 2 (NEW): The LHS is of type `any`.
            if (lhs_type->kind == TypeKind::ANY) {
                // The operation is valid. The result type of `any ?? <something>` is always `any`,
                // as we cannot know at compile time if the default value will be used.
                pushAndSave(&expr, m_type_any);
                return {};
            }

            // If neither rule matches, it's an error.
            error(expr.op, "The left-hand side of the '??' operator must be an optional type (e.g., 'string?') or 'any', but got '" + lhs_type->toString() + "'.");
            pushAndSave(&expr, m_type_error);
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

        // Warn if a nil literal is used in a logical expression — it always evaluates to false.
        if (left_type->kind == TypeKind::NIL) {
                        warning(expr.op, "Using 'nil' in a logical expression always evaluates to false.", "W002");
        } else if (right_type->kind == TypeKind::NIL) {
            warning(expr.op, "Using 'nil' in a logical expression always evaluates to false.", "W002");
        }

        // The result of a logical '&&' or '||' expression is always a boolean.
        pushAndSave(&expr, m_type_bool);
        return {};
    }

}