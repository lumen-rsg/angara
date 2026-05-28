#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const Unary& expr) {
        expr.right->accept(*this);
        auto right_type = popType();

        std::shared_ptr<Type> result_type = m_type_error;
        switch (expr.op.type) {
            case TokenType::MINUS:
                if (isNumeric(right_type)) result_type = right_type;
                else error(expr.op, "Operator '-' requires a numeric operand, but got '" + right_type->toString() + "'.", "E359");
                break;
            case TokenType::BANG:
                if (right_type->toString() == "bool") result_type = m_type_bool;
                else error(expr.op, "Operator '!' requires a boolean operand, but got '" + right_type->toString() + "'.", "E360");
                break;
            case TokenType::TILDE:
                if (isNumeric(right_type)) result_type = m_type_i64;
                else error(expr.op, "Operator '~' requires a numeric operand, but got '" + right_type->toString() + "'.", "E361");
                break;
            default: ;
        }
        pushAndSave(&expr, result_type);
        return {};
    }

}
