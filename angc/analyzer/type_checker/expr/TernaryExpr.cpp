//
// Created by cv2 on 9/19/25.
//
#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const TernaryExpr& expr) {
        // 1. Type check the condition.
        expr.condition->accept(*this);
        auto condition_type = popType();

        // 2. Type check the 'then' and 'else' branches.
        expr.thenBranch->accept(*this);
        auto then_type = popType();
        expr.elseBranch->accept(*this);
        auto else_type = popType();

        // 3. Prevent cascading errors.
        if (condition_type->kind == TypeKind::ERROR ||
            then_type->kind == TypeKind::ERROR ||
            else_type->kind == TypeKind::ERROR) {

            pushAndSave(&expr, m_type_error);
            return {};
            }

        // --- RULE 1: Condition must be a boolean ---
        if (!isTruthy(condition_type)) {
            error(Token(), "Ternary condition must be of type 'bool', but got '" +
                           condition_type->toString() + "'."); // Placeholder token
            pushAndSave(&expr, m_type_error);
            return {};
        }

        // --- RULE 2: Then and Else branches must have the same type ---
        if (then_type->toString() != else_type->toString()) {
            error(Token(), "Type mismatch in ternary expression. The 'then' branch has type '" +
                           then_type->toString() + "', but the 'else' branch has type '" +
                           else_type->toString() + "'. Both branches must have the same type.");
            pushAndSave(&expr, m_type_error);
            return {};
        }

        // --- RULE 3: The result of the expression is the type of the branches ---
        pushAndSave(&expr, then_type);

        return {};
    }

}
