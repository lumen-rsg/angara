//
// Created by cv2 on 9/20/25.
//

#include "CTranspiler.h"

namespace angara {

    std::string CTranspiler::transpileMatchExpr(const MatchExpr& expr) {
        auto condition_type = m_type_checker.m_expression_types.at(expr.condition.get());
        std::string enum_c_name = "Angara_" + condition_type->toString();

        std::stringstream ss;
        ss << "({ "; // Start GCC/Clang statement expression
        ss << "AngaraObject __match_val = " << transpileExpr(expr.condition) << "; ";
        ss << "AngaraObject __match_result; ";
        ss << "switch (((" << enum_c_name << "*)AS_OBJ(__match_val))->tag) { ";

        for (const auto& case_item : expr.cases) {
            // Handle the wildcard case `_`
            if (auto var_expr = std::dynamic_pointer_cast<const VarExpr>(case_item.pattern)) {
                if (var_expr->name.lexeme == "_") {
                    ss << "default: { ";
                    ss << "__match_result = " << transpileExpr(case_item.body) << "; ";
                    ss << "break; } ";
                    continue;
                }
            }

            // Handle a regular enum variant case
            if (auto get_expr = std::dynamic_pointer_cast<const GetExpr>(case_item.pattern)) {
                const std::string& variant_name = get_expr->name.lexeme;
                ss << "case " << enum_c_name << "_Tag_" << variant_name << ": { ";

                // Handle destructuring
                if (case_item.variable) {
                    ss << "AngaraObject " << sanitize_name(case_item.variable->lexeme) << " = ";
                    ss << "((" << enum_c_name << "*)AS_OBJ(__match_val))->payload." << sanitize_name(variant_name) << "; ";
                }

                ss << "__match_result = " << transpileExpr(case_item.body) << "; ";
                ss << "break; } ";
            }
        }

        ss << "} ";
        ss << "__match_result; "; // The last statement is the value of the expression
        ss << "})"; // End statement expression
        return ss.str();
    }

} // namespace angara