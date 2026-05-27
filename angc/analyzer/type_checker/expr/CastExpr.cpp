#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const CastExpr& expr) {
        expr.object->accept(*this);
        auto source_type = popType();

        auto target_type = resolveType(expr.target);

        // Allow: pointer-to-pointer casts, integer-to-pointer, pointer-to-integer,
        // and numeric-to-numeric casts
        bool valid = false;
        if (source_type->kind == TypeKind::POINTER && target_type->kind == TypeKind::POINTER) {
            valid = true; // pointer reinterpret
        } else if (isNumeric(source_type) && target_type->kind == TypeKind::POINTER) {
            valid = true; // int → pointer
        } else if (source_type->kind == TypeKind::POINTER && isNumeric(target_type)) {
            valid = true; // pointer → int
        } else if (isNumeric(source_type) && isNumeric(target_type)) {
            valid = true; // numeric truncation/extension
        } else if (source_type->kind == TypeKind::DATA && target_type->kind == TypeKind::POINTER) {
            valid = true; // foreign data → pointer (extracts raw ptr)
        }

        if (!valid) {
            error(expr.keyword, "Cannot cast from '" + source_type->toString() +
                "' to '" + target_type->toString() + "'.");
        }

        pushAndSave(&expr, target_type);
        return {};
    }

}
