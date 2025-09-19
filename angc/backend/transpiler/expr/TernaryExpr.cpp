//
// Created by cv2 on 9/19/25.
//
#include "CTranspiler.h"
namespace angara {

    std::string CTranspiler::transpileTernary(const TernaryExpr& expr) {
        std::string cond_str = "angara_is_truthy(" + transpileExpr(expr.condition) + ")";
        std::string then_str = transpileExpr(expr.thenBranch);
        std::string else_str = transpileExpr(expr.elseBranch);

        // C's ternary operator is a perfect match.
        return "(" + cond_str + " ? " + then_str + " : " + else_str + ")";
    }

}