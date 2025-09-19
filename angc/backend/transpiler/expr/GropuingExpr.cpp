//
// Created by cv2 on 9/19/25.
//
#include "CTranspiler.h"
namespace angara {

    std::string CTranspiler::transpileGrouping(const Grouping& expr) {
        // A grouping expression in Angara is also a grouping expression in C.
        return "(" + transpileExpr(expr.expression) + ")";
    }

}