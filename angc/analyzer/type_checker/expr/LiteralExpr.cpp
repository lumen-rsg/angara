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
                // Freestanding gate (E915): strings are heap-allocated and need the
                // string runtime, neither of which exists on bare metal. Drive MMIO
                // with peek/poke and fixed-size i8 buffers instead.
                if (m_is_in_freestanding_mode) {
                    error(expr.token,
                          "String literals are not available in --freestanding mode "
                          "(strings require heap allocation and the string runtime). "
                          "Use a byte-string literal (b\"...\") with a fixed-size "
                          "[u8; N] const instead.",
                          "E915");
                    pushAndSave(&expr, m_type_error);
                    return {};
                }
                type = m_type_string; break;
            case TokenType::BYTE_STRING:  // LANG-6
                // F3: a byte-string literal lowers to a .rodata [N x i8] global —
                // no heap, no string runtime — so it is the one string-like form
                // permitted in --freestanding mode. It is only valid where a
                // [u8; N] / [i8; N] fixed-array type is expected (flowed down via
                // m_expected_type from a `const ... as [u8; N]` annotation); a bare
                // b"..." with no array target is still a heap string and errors.
                if (m_is_in_freestanding_mode) {
                    bool is_byte_array_target = false;
                    int  declared_size = 0;
                    std::shared_ptr<Type> elem_type;
                    if (m_expected_type && m_expected_type->kind == TypeKind::FIXED_ARRAY) {
                        auto fa = std::dynamic_pointer_cast<FixedArrayType>(m_expected_type);
                        if (fa && fa->element_type &&
                            (fa->element_type->toString() == "u8" ||
                             fa->element_type->toString() == "i8")) {
                            is_byte_array_target = true;
                            declared_size = fa->size;
                            elem_type = fa->element_type;
                        }
                    }
                    if (!is_byte_array_target) {
                        error(expr.token,
                              "A byte-string literal in --freestanding mode must be "
                              "assigned to a fixed-size [u8; N] or [i8; N] const "
                              "(e.g. `const MSG as [u8; 5] = b\"hello\";`).",
                              "E915");
                        pushAndSave(&expr, m_type_error);
                        return {};
                    }
                    // Length check: the literal must exactly fill the array.
                    auto actual = static_cast<int>(expr.token.lexeme.size());
                    if (actual != declared_size) {
                        error(expr.token,
                              "Byte-string literal has length " + std::to_string(actual) +
                              " but the target array has size " + std::to_string(declared_size) +
                              ". Adjust the literal or the [u8; N] annotation.",
                              "E275");
                        pushAndSave(&expr, m_type_error);
                        return {};
                    }
                    type = std::make_shared<FixedArrayType>(elem_type, declared_size);
                    break;
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
