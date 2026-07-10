#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const ListExpr& expr) {
        // Freestanding gate (E916): lists are heap-allocated and need the list
        // runtime, neither of which exists on bare metal.
        if (m_is_in_freestanding_mode) {
            error(Token(),
                  "List literals are not available in --freestanding mode "
                  "(lists require heap allocation and the list runtime). "
                  "Use fixed-size i8 arrays or preallocated buffers instead.",
                  "E916");
            pushAndSave(&expr, m_type_error);
            return {};
        }
        if (expr.elements.empty()) {
            // Bidirectional inference: if an expected type is a list<T>, use T as element type
            std::shared_ptr<Type> element_type = m_type_any;
            if (m_expected_type) {
                if (m_expected_type->kind == TypeKind::LIST) {
                    auto expected_list = std::dynamic_pointer_cast<ListType>(m_expected_type);
                    if (expected_list) element_type = expected_list->element_type;
                } else if (m_expected_type->kind == TypeKind::RAW_ARRAY) {
                    // SIMD-1: empty literal with raw array expected type
                    auto expected_raw = std::dynamic_pointer_cast<RawArrayType>(m_expected_type);
                    if (expected_raw) {
                        element_type = expected_raw->element_type;
                        auto raw_arr_type = std::make_shared<RawArrayType>(element_type);
                        pushAndSave(&expr, raw_arr_type);
                        return {};
                    }
                }
            }
            auto empty_list_type = std::make_shared<ListType>(element_type);
            pushAndSave(&expr, empty_list_type);
            return {};
        }

        expr.elements[0]->accept(*this);
        auto common_element_type = popType();

        for (size_t i = 1; i < expr.elements.size(); ++i) {
            expr.elements[i]->accept(*this);
            auto current_element_type = popType();

            if (!sameType(common_element_type, current_element_type)) {
                common_element_type = m_type_any;
            }

            if (common_element_type->kind == TypeKind::ANY) {
                for (size_t j = i + 1; j < expr.elements.size(); ++j) {
                    expr.elements[j]->accept(*this);
                    popType();
                }
                break;
            }
        }

        // SIMD-1: if the expected type is a raw array, produce RawArrayType
        if (m_expected_type && m_expected_type->kind == TypeKind::RAW_ARRAY) {
            auto final_raw_type = std::make_shared<RawArrayType>(common_element_type);
            pushAndSave(&expr, final_raw_type);
            return {};
        }

        auto final_list_type = std::make_shared<ListType>(common_element_type);
        pushAndSave(&expr, final_list_type);

        return {};
    }

}
