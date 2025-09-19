//
// Created by cv2 on 9/19/25.
//

#include "CTranspiler.h"
namespace angara {

    std::string CTranspiler::transpileSubscriptExpr(const SubscriptExpr& expr) {
        // 1. Get the pre-computed type of the object being accessed.
        auto collection_type = m_type_checker.m_expression_types.at(expr.object.get());
        std::string object_str = transpileExpr(expr.object);

        // 2. Dispatch based on the collection's type.
        if (collection_type->kind == TypeKind::LIST) {
            std::string index_str = transpileExpr(expr.index);
            return "angara_list_get(" + object_str + ", " + index_str + ")";
        }

        if (collection_type->kind == TypeKind::RECORD) {
            std::string index_str = transpileExpr(expr.index);
            // This now works for both literals (which become Angara strings) and variables.
            return "angara_record_get_with_angara_key(" + object_str + ", " + index_str + ")";
        }

        // Fallback if the type checker somehow let a non-subscriptable type through.
        return "/* unsupported subscript */";
    }

}