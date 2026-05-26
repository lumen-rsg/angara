#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const RetypeExpr& expr) {
        auto target_type = resolveType(expr.target_type);

        expr.expression->accept(*this);
        auto source_type = popType();

        if (target_type->kind == TypeKind::ERROR || source_type->kind == TypeKind::ERROR) {
            pushAndSave(&expr, m_type_error);
            return {};
        }

        if (source_type->kind != TypeKind::C_PTR) {
            error(expr.keyword, "'retype' can only be applied to an expression of type 'c_ptr', but got '" + source_type->toString() + "'.");
        }

        if (target_type->kind != TypeKind::DATA || !std::dynamic_pointer_cast<DataType>(target_type)->is_foreign) {
            error(expr.keyword, "The target of 'retype' must be a 'foreign data' type, but got '" + target_type->toString() + "'.");
        }

        pushAndSave(&expr, target_type);

        return {};
    }

}
