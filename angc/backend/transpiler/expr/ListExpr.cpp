//
// Created by cv2 on 9/19/25.
//
#include "CTranspiler.h"
namespace angara {

    std::string CTranspiler::transpileListExpr(const ListExpr& expr) {
        if (expr.elements.empty()) {
            return "angara_list_new()";
        }

        std::stringstream elements_ss;
        for (size_t i = 0; i < expr.elements.size(); ++i) {
            elements_ss << transpileExpr(expr.elements[i]);
            if (i < expr.elements.size() - 1) {
                elements_ss << ", ";
            }
        }
        return "angara_list_new_with_elements(" +
               std::to_string(expr.elements.size()) + ", " +
               "(AngaraObject[]){" + elements_ss.str() + "})";
    }

}