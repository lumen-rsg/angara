//
// Created by cv2 on 9/19/25.
//

#include "CTranspiler.h"
namespace angara{

    std::string CTranspiler::transpileAssignExpr(const AssignExpr& expr) {
        auto rhs_str = transpileExpr(expr.value);
        auto lhs_str = transpileExpr(expr.target);

            if (auto subscript_target = std::dynamic_pointer_cast<const SubscriptExpr>(expr.target)) {
                // This is a special case that doesn't transpile to a simple C assignment.
                // We must generate a call to a runtime setter function.

                std::string object_str = transpileExpr(subscript_target->object);
                std::string value_str = transpileExpr(expr.value);
                auto collection_type = m_type_checker.m_expression_types.at(subscript_target->object.get());

                if (collection_type->kind == TypeKind::LIST) {
                    std::string index_str = transpileExpr(subscript_target->index);
                    // Generates: angara_list_set(list, index, value);
                    return "angara_list_set(" + object_str + ", " + index_str + ", " + value_str + ")";
                }

                if (collection_type->kind == TypeKind::RECORD) {
                    std::string index_str = transpileExpr(subscript_target->index);
                    return "angara_record_set_with_angara_key(" + object_str + ", " + index_str + ", " + value_str + ")";
                }

                return "/* unsupported subscript assignment */";
            }

        if (expr.op.type == TokenType::EQUAL) {
            // Simple assignment: x = y
            return "(" + lhs_str + " = " + rhs_str + ")";
        } else {
            // Compound assignment: x += y, x -= y, etc.
            // This desugars to: x = x + y

            // 1. Get the core operator string (e.g., "+" from "+=").
            std::string core_op = expr.op.lexeme;
            core_op.pop_back(); // Remove the trailing '='

            // 2. Get the type of the LHS to generate the correct unboxing/reboxing.
            auto target_type = m_type_checker.m_expression_types.at(expr.target.get());

            // 3. Assemble the C expression.
            std::string full_expression;
            if (isInteger(target_type)) {
                // e.g., create_i64((AS_I64(x) + AS_I64(y)))
                full_expression = "angara_create_i64((AS_I64(" + lhs_str + ") " + core_op + " AS_I64(" + rhs_str + ")))";
            } else if (isFloat(target_type)) {
                // e.g., create_f64((AS_F64(x) + AS_F64(y)))
                // Note: This correctly handles the case where one is an int and one is a float
                // because our AS_F64 macro performs the promotion.
                full_expression = "angara_create_f64((AS_F64(" + lhs_str + ") " + core_op + " AS_F64(" + rhs_str + ")))";
            } else if (target_type->toString() == "string" && expr.op.type == TokenType::PLUS_EQUAL) {
                // String concatenation: x = angara_string_concat(x, y)
                full_expression = "angara_string_concat(" + lhs_str + ", " + rhs_str + ")";
            } else {
                // Should be unreachable if the Type Checker is correct.
                full_expression = "angara_create_nil() /* unsupported compound assignment */";
            }

            // 4. Return the full assignment expression.
            return "(" + lhs_str + " = " + full_expression + ")";
        }
    }

}