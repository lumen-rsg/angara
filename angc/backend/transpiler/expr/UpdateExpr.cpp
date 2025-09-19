//
// Created by cv2 on 9/19/25.
//

#include "CTranspiler.h"
namespace angara {

    std::string CTranspiler::transpileUpdate(const UpdateExpr& expr) {
        // We can only increment/decrement variables, which are valid l-values.
        // The C code needs to pass the ADDRESS of the variable to the runtime helper.
        std::string target_str = transpileExpr(expr.target);

        if (expr.op.type == TokenType::PLUS_PLUS) {
            if (expr.isPrefix) {
                // ++i  ->  angara_pre_increment(&i)
                return "angara_pre_increment(&" + target_str + ")";
            } else {
                // i++  ->  angara_post_increment(&i)
                return "angara_post_increment(&" + target_str + ")";
            }
        } else { // MINUS_MINUS
            if (expr.isPrefix) {
                // --i  ->  angara_pre_decrement(&i)
                return "angara_pre_decrement(&" + target_str + ")";
            } else {
                // i--  ->  angara_post_decrement(&i)
                return "angara_post_decrement(&" + target_str + ")";
            }
        }
    }

}