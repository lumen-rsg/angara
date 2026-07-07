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
            if (!isInUnsafeContext()) {
                error(expr.keyword, "Integer-to-pointer cast requires @unsafe context.", "E469");
            }
            valid = true; // int → pointer
        } else if (source_type->kind == TypeKind::POINTER && isNumeric(target_type)) {
            if (!isInUnsafeContext()) {
                error(expr.keyword, "Pointer-to-integer cast requires @unsafe context.", "E469");
            }
            valid = true; // pointer → int
        } else if (isNumeric(source_type) && isNumeric(target_type)) {
            valid = true; // numeric truncation/extension
            // Warn on narrowing integer casts in safe code.
            if (isInteger(source_type) && isInteger(target_type) &&
                classifyIntConv(target_type, source_type) == IntConv::Narrow &&
                !isInUnsafeContext()) {
                error(expr.keyword, "Narrowing integer cast from '" +
                    source_type->toString() + "' to '" + target_type->toString() +
                    "' may lose data. Use @unsafe to suppress.", "E470");
            }
        } else if (source_type->kind == TypeKind::DATA && target_type->kind == TypeKind::POINTER) {
            valid = true; // foreign data → pointer (extracts raw ptr)
        }

        if (!valid) {
            error(expr.keyword, "Cannot cast from '" + source_type->toString() +
                "' to '" + target_type->toString() + "'.", "E378");
        }

        pushAndSave(&expr, target_type);
        return {};
    }

}
