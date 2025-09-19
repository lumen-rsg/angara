//
// Created by cv2 on 9/19/25.
//
#include "CTranspiler.h"
namespace angara {

    std::string CTranspiler::transpileRecordExpr(const RecordExpr& expr) {
        if (expr.keys.empty()) {
            return "angara_record_new()";
        }

        std::stringstream kvs_ss;
        for (size_t i = 0; i < expr.keys.size(); ++i) {
            kvs_ss << "angara_string_from_c(\"" << escape_c_string(expr.keys[i].lexeme) << "\")";
            kvs_ss << ", ";
            kvs_ss << transpileExpr(expr.values[i]);
            if (i < expr.keys.size() - 1) {
                kvs_ss << ", ";
            }
        }
        return "angara_record_new_with_fields(" +
               std::to_string(expr.keys.size()) + ", " +
               "(AngaraObject[]){" + kvs_ss.str() + "})";
    }

}