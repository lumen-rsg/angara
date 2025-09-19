//
// Created by cv2 on 9/19/25.
//
#include "CTranspiler.h"
namespace angara {

    std::string CTranspiler::transpileBinary(const Binary& expr) {
        // 1. Recursively transpile the left and right sub-expressions.
        std::string lhs_str = transpileExpr(expr.left);
        std::string rhs_str = transpileExpr(expr.right);
        std::string op = expr.op.lexeme;

        // 2. Get the pre-computed types of the operands from the Type Checker.
        auto lhs_type = m_type_checker.m_expression_types.at(expr.left.get());
        auto rhs_type = m_type_checker.m_expression_types.at(expr.right.get());

        // 3. Dispatch based on the operator type.
        switch (expr.op.type) {
            // --- Equality Operators (handle all types) ---
            case TokenType::EQUAL_EQUAL:
            case TokenType::BANG_EQUAL: {
                std::string result_str;
                if (lhs_type->kind == TypeKind::DATA && rhs_type->kind == TypeKind::DATA) {
                    std::string c_struct_name = "Angara_" + lhs_type->toString();
                    std::string equals_func = c_struct_name + "_equals";

                    // We must first cast the AngaraObject's internal `obj` pointer to the correct
                    // C struct pointer type *before* taking its address.
                    std::string ptr_a = "((" + c_struct_name + "*)AS_OBJ(" + lhs_str + "))";
                    std::string ptr_b = "((" + c_struct_name + "*)AS_OBJ(" + rhs_str + "))";

                    result_str = equals_func + "(" + ptr_a + ", " + ptr_b + ")";

                    // The result of the _equals helper is a raw C bool. We must re-box it.
                    result_str = "angara_create_bool(" + result_str + ")";

                } else {
                    // All other types use the generic runtime equality function.
                    result_str = "angara_equals(" + lhs_str + ", " + rhs_str + ")";
                }

                if (expr.op.type == TokenType::BANG_EQUAL) {
                    return "angara_create_bool(!AS_BOOL(" + result_str + "))";
                }
                return result_str;
            }

                // --- Comparison Operators (numeric only) ---
            case TokenType::GREATER:
            case TokenType::GREATER_EQUAL:
            case TokenType::LESS:
            case TokenType::LESS_EQUAL:
                // The Type Checker guarantees these are numeric. We promote to float for a safe comparison.
                return "angara_create_bool((AS_F64(" + lhs_str + ") " + op + " AS_F64(" + rhs_str + ")))";

                // --- Arithmetic Operators (numeric only) ---
            case TokenType::PLUS:
                if (lhs_type->toString() == "string" && rhs_type->toString() == "string") {
                    return "angara_string_concat(" + lhs_str + ", " + rhs_str + ")";
                }
                // Fallthrough for numeric types
                if (isFloat(lhs_type) || isFloat(rhs_type)) {
                    return "angara_create_f64((AS_F64(" + lhs_str + ") " + op + " AS_F64(" + rhs_str + ")))";
                } else {
                    return "angara_create_i64((AS_I64(" + lhs_str + ") " + op + " AS_I64(" + rhs_str + ")))";
                }
            case TokenType::MINUS:
            case TokenType::STAR:
            case TokenType::SLASH:
                if (isFloat(lhs_type) || isFloat(rhs_type)) {
                    return "angara_create_f64((AS_F64(" + lhs_str + ") " + op + " AS_F64(" + rhs_str + ")))";
                } else {
                    return "angara_create_i64((AS_I64(" + lhs_str + ") " + op + " AS_I64(" + rhs_str + ")))";
                }

            case TokenType::PERCENT:
                if (isFloat(lhs_type) || isFloat(rhs_type)) {
                    return "angara_create_f64(fmod(AS_F64(" + lhs_str + "), AS_F64(" + rhs_str + ")))";
                } else {
                    return "angara_create_i64((AS_I64(" + lhs_str + ") % AS_I64(" + rhs_str + ")))";
                }

            default:
                // String concatenation is handled by the PLUS case if types match.
                // Any other operator on non-numeric types would have been caught by the Type Checker.
                return "angara_create_nil() /* unreachable */";
        }
    }

}