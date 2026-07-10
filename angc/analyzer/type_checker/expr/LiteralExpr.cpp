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
            case TokenType::STRING:
            case TokenType::RAW_STRING:   // LANG-6
            case TokenType::BYTE_STRING:  // LANG-6
                // Freestanding gate (E915): strings are heap-allocated and need the
                // string runtime, neither of which exists on bare metal. Drive MMIO
                // with peek/poke and fixed-size i8 buffers instead.
                if (m_is_in_freestanding_mode) {
                    error(expr.token,
                          "String literals are not available in --freestanding mode "
                          "(strings require heap allocation and the string runtime). "
                          "Use fixed-size i8 buffers with peek/poke instead.",
                          "E915");
                    pushAndSave(&expr, m_type_error);
                    return {};
                }
                type = m_type_string; break;
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
