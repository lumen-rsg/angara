#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const ListExpr& expr) {
        if (expr.elements.empty()) {
            auto empty_list_type = std::make_shared<ListType>(m_type_any);
            pushAndSave(&expr, empty_list_type);
            return {};
        }

        expr.elements[0]->accept(*this);
        auto common_element_type = popType();

        for (size_t i = 1; i < expr.elements.size(); ++i) {
            expr.elements[i]->accept(*this);
            auto current_element_type = popType();

            if (common_element_type->toString() != current_element_type->toString()) {
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
