#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const UpdateExpr& expr) {
        expr.target->accept(*this);
        auto target_type = popType();

        if (!isNumeric(target_type)) {
            error(expr.op, "Operator '" + expr.op.lexeme + "' requires a numeric operand, but got '" +
                           target_type->toString() + "'.");
            pushAndSave(&expr, m_type_error);
            return {};
        }

        if (auto var_target = std::dynamic_pointer_cast<const VarExpr>(expr.target)) {
            auto symbol = m_symbols.resolve(var_target->name.lexeme);
            if (symbol && symbol->is_const) {
                error(expr.op, "Cannot modify 'const' variable '" + symbol->name + "' with '" + expr.op.lexeme + "'.");
            }
        } else {
            error(expr.op, "Operator '" + expr.op.lexeme + "' can only be applied to a variable.");
            pushAndSave(&expr, m_type_error);
            return {};
        }

        pushAndSave(&expr, target_type);
        return {};
    }

}
