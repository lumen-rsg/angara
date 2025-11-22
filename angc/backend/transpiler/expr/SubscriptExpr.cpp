#include "CTranspiler.h"

namespace angara {

    std::string CTranspiler::transpileSubscriptExpr(const SubscriptExpr& expr) {
        // 1. Get the type of the object being accessed.
        auto collection_type = m_type_checker.m_expression_types.at(expr.object.get());
        std::string object_str = transpileExpr(expr.object);
        std::string index_str = transpileExpr(expr.index);

        // --- NEW: Handle Dynamic Access on `any` ---
        if (collection_type->kind == TypeKind::ANY) {
            // Generates a call to our new runtime polymorphic getter.
            // This works for both lists (with int index) and records (with string key).
            return "angara_get(" + object_str + ", " + index_str + ")";
        }
        // --- END NEW ---

        // 2. Dispatch based on the static type (Existing logic).
        if (collection_type->kind == TypeKind::LIST) {
            return "angara_list_get(" + object_str + ", " + index_str + ")";
        }

        if (collection_type->kind == TypeKind::RECORD) {
            return "angara_record_get_with_angara_key(" + object_str + ", " + index_str + ")";
        }

        return "/* unsupported subscript */";
    }

}