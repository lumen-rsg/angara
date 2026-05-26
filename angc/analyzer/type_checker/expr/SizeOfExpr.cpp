#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const SizeofExpr& expr) {
        auto resolved_type = resolveType(expr.type_arg);

        if (resolved_type->kind == TypeKind::ERROR) {
            pushAndSave(&expr, m_type_error);
            return {};
        }

        m_sizeof_resolutions[&expr] = resolved_type;

        pushAndSave(&expr, m_type_u64);

        return {};
    }

}
