#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const Binary& expr) {
        expr.left->accept(*this);
        auto left_type = popType();
        expr.right->accept(*this);
        auto right_type = popType();

        std::shared_ptr<Type> result_type = m_type_error;

        if (left_type->kind == TypeKind::ERROR || right_type->kind == TypeKind::ERROR) {
            pushAndSave(&expr, m_type_error);
            return {};
        }

        switch (expr.op.type) {
            case TokenType::MINUS:
            case TokenType::SLASH:
            case TokenType::PERCENT:
            case TokenType::AMPERSAND:
            case TokenType::PIPE:
            case TokenType::CARET:
            case TokenType::LSHIFT:
            case TokenType::RSHIFT:
                if (m_is_in_unsafe_context && (left_type->kind == TypeKind::ANY || right_type->kind == TypeKind::ANY)) {
                    result_type = m_type_any;
                } else if (isNumeric(left_type) && isNumeric(right_type)) {
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
                        auto left_name = left_type->toString();
                        auto right_name = right_type->toString();
                        if (left_name == "i64" || right_name == "i64") {
                            result_type = m_type_i64;
                        } else if (left_name == "u64" || right_name == "u64") {
                            result_type = (left_name == "u64") ? left_type : right_type;
                        } else {
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
                    error(expr.op, "Operator '" + expr.op.lexeme + "' requires numeric operands, but got '" +
                                   left_type->toString() + "' and '" + right_type->toString() + "'.", "E352");
                }
                break;

            case TokenType::STAR:
                if (m_is_in_unsafe_context && (left_type->kind == TypeKind::ANY || right_type->kind == TypeKind::ANY)) {
                    result_type = m_type_any;
                } else if (isNumeric(left_type) && isNumeric(right_type)) {
                    if (isFloat(left_type) || isFloat(right_type)) {
                        result_type = m_type_f64;
                    } else {
                        result_type = m_type_i64;
                    }
                } else if (left_type->toString() == "string" && isNumeric(right_type)) {
                    result_type = m_type_string;
                } else {
                    error(expr.op, "Operator '*' can only be used with two numbers (arithmetic) or 'string * number' (repetition).", "E353");
                }
                break;

            case TokenType::PLUS:
                if (m_is_in_unsafe_context && (left_type->kind == TypeKind::ANY || right_type->kind == TypeKind::ANY)) {
                    result_type = m_type_any;
                } else if (isNumeric(left_type) && isNumeric(right_type)) {
                    if (isFloat(left_type) || isFloat(right_type)) {
                        result_type = m_type_f64;
                    } else {
                        result_type = m_type_i64;
                    }
                } else if (left_type->toString() == "string" && right_type->toString() == "string") {
                    result_type = m_type_string;
                } else {
                    error(expr.op, "Operator '+' can only be used with two numbers (addition) or two strings (concatenation).", "E354");
                }
                break;

            case TokenType::GREATER:
            case TokenType::GREATER_EQUAL:
            case TokenType::LESS:
            case TokenType::LESS_EQUAL:
                if (m_is_in_unsafe_context && (left_type->kind == TypeKind::ANY || right_type->kind == TypeKind::ANY)) {
                    result_type = m_type_bool;
                } else if (isNumeric(left_type) && isNumeric(right_type)) {
                    result_type = m_type_bool;
                } else if (left_type->toString() == "string" && right_type->toString() == "string") {
                    result_type = m_type_bool;
                } else {
                    error(expr.op, "Operator '" + expr.op.lexeme + "' requires numeric or string operands, but got '" +
                                   left_type->toString() + "' and '" + right_type->toString() + "'.", "E355");
                }
                break;

            case TokenType::EQUAL_EQUAL:
            case TokenType::BANG_EQUAL: {
                if (left_type->kind == TypeKind::DATA && right_type->kind == TypeKind::DATA) {
                    if (left_type->toString() == right_type->toString()) {
                        result_type = m_type_bool;
                    } else {
                        error(expr.op, "Cannot compare instances of two different data types: '" +
                                       left_type->toString() + "' and '" + right_type->toString() + "'.", "E356");
                    }
                }
                else if (left_type->toString() == right_type->toString() ||
                    left_type->kind == TypeKind::ANY || right_type->kind == TypeKind::ANY ||
                    left_type->kind == TypeKind::NIL || right_type->kind == TypeKind::NIL ||
                    (isNumeric(left_type) && isNumeric(right_type)))
                {
                    result_type = m_type_bool;
                } else {
                    error(expr.op, "Cannot compare types '" +
                                   left_type->toString() + "' and '" + right_type->toString() + "'.", "E357");
                }
                break;
            }

            default:
                error(expr.op, "Unknown binary operator '" + expr.op.lexeme + "'.", "E358");
                break;
        }

        pushAndSave(&expr, result_type);
        return {};
    }

}
