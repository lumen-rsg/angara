#include "TypeChecker.h"

namespace angara {

    std::any TypeChecker::visit(const SizeofExpr& expr) {
        // 1. Resolve the type argument inside the <...>.
        auto resolved_type = resolveType(expr.type_arg);

        // 2. Validate the type.
        if (resolved_type->kind == TypeKind::ERROR) {
            // resolveType already reported the error.
            pushAndSave(&expr, m_type_error);
            return {};
        }

        // For now, we allow sizeof on any concrete type.
        // We could add restrictions later (e.g., forbid sizeof on contracts).

        // 3. Store the resolved inner type for the transpiler to use later.
        m_sizeof_resolutions[&expr] = resolved_type;

        // 4. The result of a sizeof expression is always a u64.
        pushAndSave(&expr, m_type_u64);

        return {};
    }

} // namespace angara