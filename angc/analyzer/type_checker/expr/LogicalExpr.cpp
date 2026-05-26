#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const LogicalExpr& expr) {
        if (expr.op.type == TokenType::QUESTION_QUESTION) {
            expr.left->accept(*this);
            auto lhs_type = popType();
            expr.right->accept(*this);
            auto rhs_type = popType();

            if (lhs_type->kind == TypeKind::ERROR || rhs_type->kind == TypeKind::ERROR) {
                pushAndSave(&expr, m_type_error);
                return {};
            }

            if (lhs_type->kind == TypeKind::OPTIONAL) {
                auto unwrapped_lhs_type = std::dynamic_pointer_cast<OptionalType>(lhs_type)->wrapped_type;
                if (!check_type_compatibility(unwrapped_lhs_type, rhs_type)) {
                    error(expr.op, "Type mismatch in '??' operator. The default value has type '" + rhs_type->toString() +
                                   "', but the unwrapped optional expects type '" + unwrapped_lhs_type->toString() + "'.");
                    pushAndSave(&expr, m_type_error);
                    return {};
                }
                pushAndSave(&expr, unwrapped_lhs_type);
                return {};
            }

            if (lhs_type->kind == TypeKind::ANY) {
                pushAndSave(&expr, m_type_any);
                return {};
            }

            error(expr.op, "The left-hand side of '??' must be an optional type (e.g., 'string?') or 'any', but got '" + lhs_type->toString() + "'.");
            pushAndSave(&expr, m_type_error);
            return {};
        }

        expr.left->accept(*this);
        auto left_type = popType();
        expr.right->accept(*this);
        auto right_type = popType();

        if (left_type->kind == TypeKind::ERROR || right_type->kind == TypeKind::ERROR) {
            pushAndSave(&expr, m_type_error);
            return {};
        }

        if (!isTruthy(left_type) || !isTruthy(right_type)) {
            error(expr.op, "Logical operator '" + expr.op.lexeme + "' requires truthy operands, but got '" +
                           left_type->toString() + "' and '" + right_type->toString() + "'.");
            pushAndSave(&expr, m_type_error);
            return {};
        }

        if (left_type->kind == TypeKind::NIL) {
            warning(expr.op, "Using 'nil' in a logical expression always evaluates to false.", "W002");
        } else if (right_type->kind == TypeKind::NIL) {
            warning(expr.op, "Using 'nil' in a logical expression always evaluates to false.", "W002");
        }

        pushAndSave(&expr, m_type_bool);
        return {};
    }

}
