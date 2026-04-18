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
            case TokenType::AMPERSAND:
            case TokenType::PIPE:
            case TokenType::CARET:
                if (isNumeric(left_type) && isNumeric(right_type)) {
                    // Warn about division/modulo by literal zero
                    if ((expr.op.type == TokenType::SLASH || expr.op.type == TokenType::PERCENT)) {
                        if (auto rhs_literal = std::dynamic_pointer_cast<const Literal>(expr.right)) {
                            if (rhs_literal->token.type == TokenType::NUMBER_INT && rhs_literal->token.lexeme == "0") {
                                warning(expr.op, std::string(expr.op.type == TokenType::SLASH
                                    ? "Division by zero."
                                    : "Modulo by zero."), "W001");
                            }
                        }
                    }
                    if (isFloat(left_type) || isFloat(right_type)) {
                        result_type = m_type_f64;
                    } else {
                        // Preserve specific integer types (i8, i16, i32, u8, u16, u32, u64)
                        // If both are i64 (default), result is i64.
                        // Otherwise, result is the wider of the two operand types.
                        auto left_name = left_type->toString();
                        auto right_name = right_type->toString();
                        if (left_name == "i64" || right_name == "i64") {
                            result_type = m_type_i64;
                        } else if (left_name == "u64" || right_name == "u64") {
                            result_type = (left_name == "u64") ? left_type : right_type;
                        } else {
                            // Return the wider type; if equal width, prefer signed
                            auto width = [](const std::string& n) -> int {
                                if (n == "i8"  || n == "u8")  return 8;
                                if (n == "i16" || n == "u16") return 16;
                                if (n == "i32" || n == "u32") return 32;
                                return 64;
                            };
                            int lw = width(left_name), rw = width(right_name);
                            if (lw >= rw) result_type = left_type;
                            else result_type = right_type;
                        }
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