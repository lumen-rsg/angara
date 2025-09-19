//
// Created by cv2 on 9/19/25.
//
#include "CTranspiler.h"
namespace angara {

    std::string CTranspiler::transpileUnary(const Unary& expr) {
        // 1. Get the pre-computed type of the operand from the Type Checker's results.
        //    This is crucial for knowing whether to generate integer or float negation.
        auto operand_type = m_type_checker.m_expression_types.at(expr.right.get());

        // 2. Recursively transpile the operand expression.
        std::string operand_str = transpileExpr(expr.right);

        switch (expr.op.type) {
            case TokenType::BANG: {
                // Transpiles `!some_expression`.
                // The logic is: get the truthiness of the Angara object, invert the
                // resulting C bool, and then re-box it as a new Angara bool object.
                return "angara_create_bool(!angara_is_truthy(" + operand_str + "))";
            }

            case TokenType::MINUS: {
                // Transpiles `-some_expression`.
                // The Type Checker has already guaranteed the operand is a number.
                // We just need to generate the correct C code for its specific type.
                if (isFloat(operand_type)) {
                    // Unbox as a double, negate, and re-box as a new f64 AngaraObject.
                    return "angara_create_f64(-AS_F64(" + operand_str + "))";
                } else {
                    // Unbox as an int64, negate, and re-box as a new i64 AngaraObject.
                    return "angara_create_i64(-AS_I64(" + operand_str + "))";
                }
            }

            default:
                // This should be unreachable if the parser and type checker are correct.
                return "angara_create_nil() /* unsupported unary op */";
        }
    }

}