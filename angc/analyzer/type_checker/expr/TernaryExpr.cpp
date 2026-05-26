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
            error(Token(), "Ternary condition must be a truthy type, but got '" +
                           condition_type->toString() + "'.");
            pushAndSave(&expr, m_type_error);
            return {};
        }

        if (then_type->toString() != else_type->toString()) {
            error(Token(), "Type mismatch in ternary expression. The 'then' branch has type '" +
                           then_type->toString() + "', but the 'else' branch has type '" +
                           else_type->toString() + "'. Both branches must return the same type.");
            pushAndSave(&expr, m_type_error);
            return {};
        }

        pushAndSave(&expr, then_type);

        return {};
    }

}
