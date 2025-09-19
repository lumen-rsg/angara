//
// Created by cv2 on 9/19/25.
//

#include "TypeChecker.h"
namespace angara {

    bool TypeChecker::check_type_compatibility(
                const std::shared_ptr<Type>& expected,
                const std::shared_ptr<Type>& actual
        ) {
        if (expected->toString() == actual->toString()) return true;

        // Rule: Anything is compatible with 'any'.
        if (expected->kind == TypeKind::ANY || actual->kind == TypeKind::ANY) return true;

        // Rule: A T or a `nil` is compatible with a T?
        if (expected->kind == TypeKind::OPTIONAL) {
            auto optional_type = std::dynamic_pointer_cast<OptionalType>(expected);
            if (optional_type->wrapped_type->toString() == actual->toString() || actual->kind == TypeKind::NIL) {
                return true;
            }
        }

        // Rule: A generic record `{}` is compatible with a specific record `{...}`.
        if (expected->kind == TypeKind::RECORD && actual->kind == TypeKind::RECORD) {
            if (std::dynamic_pointer_cast<RecordType>(expected)->fields.empty()) {
                return true;
            }
        }

        return false; // Not compatible
    }

    void TypeChecker::visit(std::shared_ptr<const ReturnStmt> stmt) {
        if (m_function_return_types.empty()) {
            error(stmt->keyword, "Cannot use 'return' outside of a function.");
            return;
        }

        auto expected_return_type = m_function_return_types.top();

        if (stmt->value) {
            // A value is being returned.
            stmt->value->accept(*this);
            auto actual_return_type = popType();


            if (!check_type_compatibility(expected_return_type, actual_return_type)) {
                error(stmt->keyword, "Type mismatch. This function is declared to return '" +
                                     expected_return_type->toString() + "', but is returning a value of type '" +
                                     actual_return_type->toString() + "'.");
            }

        } else {
            // No value is being returned ('return;').
            // This is only valid if the function is supposed to return `nil`.
            // A `nil` return is a value, and must use `return nil;`.
            // So, this case is actually an error unless the expected type is also nil,
            // which check_type_compatibility would handle. Let's make this clearer.

            // A `return;` is semantically equivalent to `return nil;`.
            if (!check_type_compatibility(expected_return_type, m_type_nil)) {
                error(stmt->keyword, "This function must return a value of type '" +
                                     expected_return_type->toString() + "'. An empty 'return;' is only valid for functions that return 'nil'.");
            }
        }
    }

}