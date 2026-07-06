#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const TernaryExpr& expr) {
        expr.condition->accept(*this);
        auto condition_type = popType();

        expr.thenBranch->accept(*this);
        auto then_type = popType();
        expr.elseBranch->accept(*this);
        auto else_type = popType();

        if (condition_type->kind == TypeKind::ERROR ||
            then_type->kind == TypeKind::ERROR ||
            else_type->kind == TypeKind::ERROR) {

            pushAndSave(&expr, m_type_error);
            return {};
            }

        if (!isTruthy(condition_type)) {
            error(expr.op, "Ternary condition must be a truthy type, but got '" +
                           condition_type->toString() + "'.", "E380");
            pushAndSave(&expr, m_type_error);
            return {};
        }

        if (!check_type_compatibility(then_type, else_type)) {
            error(expr.op, "Type mismatch in ternary expression. The 'then' branch has type '" +
                           then_type->toString() + "', but the 'else' branch has type '" +
                           else_type->toString() + "'. Both branches must return compatible types.", "E381");
            pushAndSave(&expr, m_type_error);
            return {};
        }

        pushAndSave(&expr, then_type);

        return {};
    }

}
