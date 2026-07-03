#include "TypeChecker.h"
namespace angara {

    // LANG-1: range expression type-checker. `start..end` produces a list<i64>.
    // Both bounds must be integer types.
    std::any TypeChecker::visit(const RangeExpr& expr) {
        expr.left->accept(*this);
        auto left_type = popType();

        expr.right->accept(*this);
        auto right_type = popType();

        if (left_type->kind != TypeKind::ERROR && right_type->kind != TypeKind::ERROR) {
            if (!isInteger(left_type)) {
                error(expr.op, "Range start must be an integer, but got '" + left_type->toString() + "'.", "E396");
            }
            if (!isInteger(right_type)) {
                error(expr.op, "Range end must be an integer, but got '" + right_type->toString() + "'.", "E397");
            }
        }

        // A range is typed as list<i64> everywhere (for-in, assignment, etc.).
        pushAndSave(&expr, std::make_shared<ListType>(m_type_i64));
        return {};
    }

}
