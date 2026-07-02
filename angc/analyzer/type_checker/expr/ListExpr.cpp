#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const ListExpr& expr) {
        if (expr.elements.empty()) {
            // Bidirectional inference: if an expected type is a list<T>, use T as element type
            std::shared_ptr<Type> element_type = m_type_any;
            if (m_expected_type && m_expected_type->kind == TypeKind::LIST) {
                auto expected_list = std::dynamic_pointer_cast<ListType>(m_expected_type);
                if (expected_list) {
                    element_type = expected_list->element_type;
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

        auto final_list_type = std::make_shared<ListType>(common_element_type);
        pushAndSave(&expr, final_list_type);

        return {};
    }

}
