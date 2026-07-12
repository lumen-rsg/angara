#include "TypeChecker.h"
namespace angara {

    // LANG-3: interpolated string. Visit each sub-expression for type-checking,
    // then the whole thing is type string.
    std::any TypeChecker::visit(const InterpStringExpr& expr) {
        if (m_is_in_freestanding_mode && !m_has_fs_allocator) {
            error(Token(),
                  "Interpolated strings are not available in --freestanding mode "
                  "(strings require heap allocation and the string runtime). "
                  "Format values into fixed-size i8 buffers instead.",
                  "E915");
            pushAndSave(&expr, m_type_error);
            return {};
        }
        for (const auto& [literal, sub_expr] : expr.segments) {
            if (sub_expr) {
                sub_expr->accept(*this);
                popType();  // discard — any type is acceptable in an interpolation hole
            }
        }
        pushAndSave(&expr, m_type_string);
        return {};
    }

}
