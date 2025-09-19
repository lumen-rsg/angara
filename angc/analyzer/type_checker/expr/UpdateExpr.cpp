//
// Created by cv2 on 9/19/25.
//

#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const UpdateExpr& expr) {
        // An update expression (++, --) is a combination of reading and assigning.

        // 1. Determine the type of the target.
        expr.target->accept(*this);
        auto target_type = popType();

        // 2. Enforce the rule: the target must be a numeric type.
        if (!isNumeric(target_type)) {
            error(expr.op, "Operand for increment/decrement must be a number, but got '" +
                           target_type->toString() + "'.");
            pushAndSave(&expr, m_type_error);
            return {};
        }

        // --- Check for Mutability ---
        if (auto var_target = std::dynamic_pointer_cast<const VarExpr>(expr.target)) {
            auto symbol = m_symbols.resolve(var_target->name.lexeme);
            if (symbol && symbol->is_const) { // <-- Check for symbol existence first
                error(expr.op, "Cannot modify 'const' variable '" + symbol->name + "'.");
            }
        } else {
            error(expr.op, "Invalid target for increment/decrement.");
            pushAndSave(&expr, m_type_error);
            return {};
        }

        // 3. The result type of update expression is the same as the target's type.
        pushAndSave(&expr, target_type);
        return {};
    }

}