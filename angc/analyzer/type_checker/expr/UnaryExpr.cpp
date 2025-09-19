//
// Created by cv2 on 9/19/25.
//

#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const Unary& expr) {
        expr.right->accept(*this);
        auto right_type = popType();

        std::shared_ptr<Type> result_type = m_type_error;
        switch (expr.op.type) {
            case TokenType::MINUS:
                if (isNumeric(right_type)) result_type = right_type;
                else error(expr.op, "Operand for '-' must be a number.");
                break;
            case TokenType::BANG:
                if (right_type->toString() == "bool") result_type = m_type_bool;
                else error(expr.op, "Operand for '!' must be a boolean.");
                break;
        }
        pushAndSave(&expr, result_type);
        return {};
    }

}