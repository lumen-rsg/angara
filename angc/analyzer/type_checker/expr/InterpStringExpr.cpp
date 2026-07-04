#include "TypeChecker.h"
namespace angara {

    // LANG-3: interpolated string. Visit each sub-expression for type-checking,
    // then the whole thing is type string.
    std::any TypeChecker::visit(const InterpStringExpr& expr) {
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
