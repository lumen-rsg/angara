//
// Created by cv2 on 9/19/25.
//

#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const Binary& expr) {
        // 1. Visit operands to get their types.
        expr.left->accept(*this);
        auto left_type = popType();
        expr.right->accept(*this);
        auto right_type = popType();

        // 2. Default to an error type. We only change this if a rule generators.
        std::shared_ptr<Type> result_type = m_type_error;

        // 3. Bail out early if sub-expressions had errors.
        if (left_type->kind == TypeKind::ERROR || right_type->kind == TypeKind::ERROR) {
            pushAndSave(&expr, m_type_error);
            return {};
        }

        // 4. Check the types based on the operator.
        switch (expr.op.type) {
            case TokenType::MINUS:
            case TokenType::STAR:
            case TokenType::SLASH:
            case TokenType::PERCENT:
                if (isNumeric(left_type) && isNumeric(right_type)) {
                    if (isFloat(left_type) || isFloat(right_type)) {
                        result_type = m_type_f64;
                    } else {
                        result_type = m_type_i64;
                    }
                } else {
                    error(expr.op, "Operands for this arithmetic operator must be numbers.");
                }
                break;

            case TokenType::PLUS:
                if (isNumeric(left_type) && isNumeric(right_type)) {
                    if (isFloat(left_type) || isFloat(right_type)) {
                        result_type = m_type_f64;
                    } else {
                        result_type = m_type_i64;
                    }
                } else if (left_type->toString() == "string" && right_type->toString() == "string") {
                    result_type = m_type_string;
                } else {
                    error(expr.op, "'+' operator can only be used on two numbers or two strings.");
                }
                break;

            case TokenType::GREATER:
            case TokenType::GREATER_EQUAL:
            case TokenType::LESS:
            case TokenType::LESS_EQUAL:
                if (isNumeric(left_type) && isNumeric(right_type)) {
                    result_type = m_type_bool;
                } else {
                    error(expr.op, "Operands for comparison must be numbers.");
                }
                break;

            case TokenType::EQUAL_EQUAL:
            case TokenType::BANG_EQUAL: {
                // The comparison is valid if:
                // 1. The types are exactly the same.
                // 2. One of the types is 'any' (or nil, which can be compared to anything).
                // 3. Both types are numeric (allowing i64 == f64).
                // --- NEW RULE ---
                // Two instances of the same data type can be compared.
                if (left_type->kind == TypeKind::DATA && right_type->kind == TypeKind::DATA) {
                    if (left_type->toString() == right_type->toString()) {
                        result_type = m_type_bool;
                    } else {
                        error(expr.op, "Cannot compare instances of two different data types: '" +
                                       left_type->toString() + "' and '" + right_type->toString() + "'.");
                    }
                }
                else if (left_type->toString() == right_type->toString() ||
                    left_type->kind == TypeKind::ANY || right_type->kind == TypeKind::ANY ||
                    left_type->kind == TypeKind::NIL || right_type->kind == TypeKind::NIL ||
                    (isNumeric(left_type) && isNumeric(right_type)))
                {
                    result_type = m_type_bool;
                } else {
                    error(expr.op, "Cannot compare two different types: '" +
                                   left_type->toString() + "' and '" + right_type->toString() + "'.");
                }
                break;
            }

            default:
                error(expr.op, "Unknown binary operator.");
                break;
        }

        // 5. Push the single, definitive result type for this expression.
        pushAndSave(&expr, result_type);
        return {};
    }

}