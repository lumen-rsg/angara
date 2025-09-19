//
// Created by cv2 on 9/19/25.
//

#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const ListExpr& expr) {
        // Case 1: The list is empty. Its type is `list<any>`.
        if (expr.elements.empty()) {
            auto empty_list_type = std::make_shared<ListType>(m_type_any);
            pushAndSave(&expr, empty_list_type);
            return {};
        }

        // Case 2: The list has elements. We must infer the common element type.

        // 1. Determine the type of the *first* element. This is our initial candidate.
        expr.elements[0]->accept(*this);
        auto common_element_type = popType();

        // 2. Iterate through the rest of the elements and update the common type.
        for (size_t i = 1; i < expr.elements.size(); ++i) {
            expr.elements[i]->accept(*this);
            auto current_element_type = popType();

            // If the types don't match, the new common type becomes 'any'.
            if (common_element_type->toString() != current_element_type->toString()) {
                // We can add more sophisticated rules here later, e.g., if you have
                // an i64 and an f64, the common type could be f64. For now, any
                // mismatch defaults to 'any'.
                common_element_type = m_type_any;
            }

            // If the common type is already 'any', we can stop checking,
            // as 'any' is the "top type" and won't change.
            if (common_element_type->kind == TypeKind::ANY) {
                // We still need to process the rest of the elements to populate
                // the expression types map, but we don't need to check their types.
                for (size_t j = i + 1; j < expr.elements.size(); ++j) {
                    expr.elements[j]->accept(*this);
                    popType();
                }
                break; // Exit the main checking loop.
            }
        }

        // 3. The final, inferred type of this expression is a ListType of the common type.
        auto final_list_type = std::make_shared<ListType>(common_element_type);
        pushAndSave(&expr, final_list_type);

        return {};
    }

}