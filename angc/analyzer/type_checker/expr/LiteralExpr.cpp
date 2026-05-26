#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const Literal& expr) {
        std::shared_ptr<Type> type = m_type_error;
        switch (expr.token.type) {
            case TokenType::NUMBER_INT:   type = m_type_i64; break;
            case TokenType::NUMBER_FLOAT: type = m_type_f64; break;
            case TokenType::STRING:       type = m_type_string; break;
            case TokenType::TRUE:
            case TokenType::FALSE:        type = m_type_bool; break;
            case TokenType::NIL:          type = m_type_nil; break;
            default:
                type = m_type_error;
                break;
        }
        pushAndSave(&expr, type);
        return {};
    }

}
