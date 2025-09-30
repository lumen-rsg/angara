#include "CTranspiler.h"
namespace angara {

std::string CTranspiler::transpileBinary(const Binary& expr) {
    auto lhs_type = m_type_checker.m_expression_types.at(expr.left.get());
    auto rhs_type = m_type_checker.m_expression_types.at(expr.right.get());
    const std::string& op = expr.op.lexeme;

    if (isNumeric(lhs_type) && isNumeric(rhs_type)) {
        bool result_is_float = isFloat(lhs_type) || isFloat(rhs_type);
        std::string lhs_str;
        std::string rhs_str;

        // --- CORRECT, TYPE-AWARE UNBOXING ---
        // Unbox the left-hand side based on its actual type.
        if (auto lit = std::dynamic_pointer_cast<const Literal>(expr.left)) {
            lhs_str = lit->token.lexeme;
        } else {
            lhs_str = result_is_float ? "AS_F64(" + transpileExpr(expr.left) + ")"
                                      : "AS_I64(" + transpileExpr(expr.left) + ")";
        }

        // Unbox the right-hand side based on its actual type.
        if (auto lit = std::dynamic_pointer_cast<const Literal>(expr.right)) {
            rhs_str = lit->token.lexeme;
        } else {
            rhs_str = result_is_float ? "AS_F64(" + transpileExpr(expr.right) + ")"
                                      : "AS_I64(" + transpileExpr(expr.right) + ")";
        }
        // --- END CORRECTION ---

        switch (expr.op.type) {
            case TokenType::PLUS:
            case TokenType::MINUS:
            case TokenType::STAR:
            case TokenType::SLASH:
                if (result_is_float) {
                    return "angara_create_f64(" + lhs_str + " " + op + " " + rhs_str + ")";
                } else {
                    return "angara_create_i64(" + lhs_str + " " + op + " " + rhs_str + ")";
                }

            case TokenType::PERCENT:
                 if (result_is_float) {
                    return "angara_create_f64(fmod(" + lhs_str + ", " + rhs_str + "))";
                } else {
                    // Use explicit casts to int64_t for safety with literals.
                    return "angara_create_i64(((int64_t)(" + lhs_str + ")) % ((int64_t)(" + rhs_str + ")))";
                }

            case TokenType::GREATER:
            case TokenType::GREATER_EQUAL:
            case TokenType::LESS:
            case TokenType::LESS_EQUAL:
                 // Comparisons are safer when promoted to double.
                 if (!isFloat(lhs_type)) lhs_str = "AS_I64(" + transpileExpr(expr.left) + ")";
                 if (!isFloat(rhs_type)) rhs_str = "AS_I64(" + transpileExpr(expr.right) + ")";
                 return "angara_create_bool((double)" + lhs_str + " " + op + " (double)" + rhs_str + ")";

            default:
                break;
        }
    }

    // --- FALLBACK PATH for Equality, String Concat, and other non-optimizable operations ---
    std::string lhs_str = transpileExpr(expr.left);
    std::string rhs_str = transpileExpr(expr.right);

    switch (expr.op.type) {
        case TokenType::EQUAL_EQUAL:
        case TokenType::BANG_EQUAL: {
            std::string result_str;
            if (lhs_type->kind == TypeKind::DATA && rhs_type->kind == TypeKind::DATA) {
                std::string c_struct_name = "Angara_" + lhs_type->toString();
                std::string equals_func = c_struct_name + "_equals";
                std::string ptr_a = "((" + c_struct_name + "*)AS_OBJ(" + lhs_str + "))";
                std::string ptr_b = "((" + c_struct_name + "*)AS_OBJ(" + rhs_str + "))";
                result_str = "angara_create_bool(" + equals_func + "(" + ptr_a + ", " + ptr_b + "))";
            } else {
                result_str = "angara_equals(" + lhs_str + ", " + rhs_str + ")";
            }

            if (expr.op.type == TokenType::BANG_EQUAL) {
                return "angara_create_bool(!AS_BOOL(" + result_str + "))";
            }
            return result_str;
        }

        case TokenType::PLUS:
            if (lhs_type->toString() == "string" && rhs_type->toString() == "string") {
                return "angara_string_concat(" + lhs_str + ", " + rhs_str + ")";
            }
            // The numeric case for '+' was handled in the optimization path above.
            // If we reach here, it's an unhandled + operation that should have been a type error.
            break;

        default:
            break;
    }

    return "angara_create_nil() /* unhandled binary op */";
}

}