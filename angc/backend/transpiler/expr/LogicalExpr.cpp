//
// Created by cv2 on 9/19/25.
//
#include "CTranspiler.h"
namespace angara {

    std::string CTranspiler::transpileLogical(const LogicalExpr& expr) {
        // --- Handle Nil Coalescing Operator `??` ---
        if (expr.op.type == TokenType::QUESTION_QUESTION) {
            std::string lhs_str = transpileExpr(expr.left);
            std::string rhs_str = transpileExpr(expr.right);
            // Generates: (!IS_NIL(lhs) ? lhs : rhs)
            return "(!IS_NIL(" + lhs_str + ") ? " + lhs_str + " : " + rhs_str + ")";
        }

        // --- logic for `&&` and `||` ---
        std::string lhs = "angara_is_truthy(" + transpileExpr(expr.left) + ")";
        std::string rhs = "angara_is_truthy(" + transpileExpr(expr.right) + ")";
        return "create_bool((" + lhs + ") " + expr.op.lexeme + " (" + rhs + "))";
    }

}