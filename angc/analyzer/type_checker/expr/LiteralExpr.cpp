#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const Literal& expr) {
        std::shared_ptr<Type> type = m_type_error;
        switch (expr.token.type) {
            case TokenType::NUMBER_INT:
                // LANG-6: suffix-driven integer type
                switch (expr.token.suffix) {
                    case LiteralSuffix::I8:  type = m_type_i8;  break;
                    case LiteralSuffix::I16: type = m_type_i16; break;
                    case LiteralSuffix::I32: type = m_type_i32; break;
                    case LiteralSuffix::I64: type = m_type_i64; break;
                    case LiteralSuffix::U8:  type = m_type_u8;  break;
                    case LiteralSuffix::U16: type = m_type_u16; break;
                    case LiteralSuffix::U32: type = m_type_u32; break;
                    case LiteralSuffix::U64: type = m_type_u64; break;
                    default:                 type = m_type_i64; break;
                }
                break;
            case TokenType::NUMBER_FLOAT: type = m_type_f64; break;
            case TokenType::STRING:       type = m_type_string; break;
            case TokenType::RAW_STRING:   type = m_type_string; break;  // LANG-6
            case TokenType::BYTE_STRING:  type = m_type_string; break;  // LANG-6
            case TokenType::CHAR:         type = m_type_char; break;  // LANG-4
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
