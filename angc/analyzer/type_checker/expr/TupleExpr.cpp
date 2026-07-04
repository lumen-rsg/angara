#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const TupleExpr& expr) {
        std::vector<std::shared_ptr<Type>> element_types;
        element_types.reserve(expr.elements.size());

        for (const auto& elem : expr.elements) {
            elem->accept(*this);
            auto elem_type = popType();
            element_types.push_back(elem_type);
        }

        auto tuple_type = std::make_shared<TupleType>(std::move(element_types));
        pushAndSave(&expr, tuple_type);
        return {};
    }

}
