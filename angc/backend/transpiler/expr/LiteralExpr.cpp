//
// Created by cv2 on 9/19/25.
//
#include "CTranspiler.h"
namespace angara {

    std::string CTranspiler::transpileLiteral(const Literal& expr) {
        auto type = m_type_checker.m_expression_types.at(&expr);
        if (type->toString() == "i64") return "angara_create_i64(" + expr.token.lexeme + "LL)";
        if (type->toString() == "f64") return "angara_create_f64(" + expr.token.lexeme + ")";
        if (type->toString() == "bool") return "angara_create_bool(" + expr.token.lexeme + ")";
        if (type->toString() == "string") {
            return "angara_string_from_c(\"" + escape_c_string(expr.token.lexeme) + "\")";
        }
        if (type->toString() == "nil") return "angara_create_nil()";
        return "angara_create_nil() /* unknown literal */";
    }

}