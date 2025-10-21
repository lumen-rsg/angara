//
// Created by cv2 on 9/19/25.
//

#include "TypeChecker.h"
namespace angara {

    // --- NEW HELPER FUNCTION ---
// This helper checks if a record literal's type structurally matches a data block's definition.
    bool TypeChecker::check_structural_match(
        const std::shared_ptr<DataType>& data_type,
        const std::shared_ptr<RecordType>& record_type
    ) {
        // For every field required by the `DataType`...
        for (const auto& [field_name, data_field_info] : data_type->fields) {
            // ...check if the `RecordType` has a field with that name.
            auto record_field_it = record_type->fields.find(field_name);
            if (record_field_it == record_type->fields.end()) {
                return false; // Missing a required field.
            }

            // ...and check if the types of those fields are compatible.
            // We call check_type_compatibility recursively to handle nested types!
            if (!check_type_compatibility(data_field_info.type, record_field_it->second)) {
                return false; // Field types do not match.
            }
        }
        // If all required fields exist and their types match, it's a structural match.
        // The record literal is allowed to have EXTRA fields.
        return true;
    }

// --- THE NEW, UNIFIED check_type_compatibility ---
bool TypeChecker::check_type_compatibility(
            const std::shared_ptr<Type>& expected,
            const std::shared_ptr<Type>& actual
) {
    // Rule 1: Exact type match is always compatible.
    if (expected->toString() == actual->toString()) return true;

    // Rule 2: Anything is compatible with 'any'.
    if (expected->kind == TypeKind::ANY || actual->kind == TypeKind::ANY) return true;

    // Rule 3: A T or a `nil` is compatible with a T?
    if (expected->kind == TypeKind::OPTIONAL) {
        auto optional_type = std::dynamic_pointer_cast<OptionalType>(expected);
        if (check_type_compatibility(optional_type->wrapped_type, actual) || actual->kind == TypeKind::NIL) {
            return true;
        }
    }

    // --- RULE 4: STRUCTURAL TYPING (THE FIX FOR BOTH BUGS) ---

    // Case A: `let x as MyData = { ... }` (DataType vs RecordType)
    if (expected->kind == TypeKind::DATA && actual->kind == TypeKind::RECORD) {
        return check_structural_match(
            std::dynamic_pointer_cast<DataType>(expected),
            std::dynamic_pointer_cast<RecordType>(actual)
        );
    }

    // Case B: Record compatibility (e.g., `let x as record = { ... }`)
    if (expected->kind == TypeKind::RECORD && actual->kind == TypeKind::RECORD) {
        auto expected_record = std::dynamic_pointer_cast<RecordType>(expected);
        auto actual_record = std::dynamic_pointer_cast<RecordType>(actual);

        // B.1 (Upcasting): Any specific record can be assigned to the generic `record` type.
        if (expected_record->fields.empty()) {
            return true; // Solves the `list<record>` issue.
        }

        // B.2 (Downcasting/Assertion): The generic `record` can be assigned to a specific record type.
        if (actual_record->fields.empty()) {
            return true;
        }
    }

    // --- RULE 5: LIST COMPATIBILITY ---
    // This now works automatically because it calls this same function on the element types.
    if (expected->kind == TypeKind::LIST && actual->kind == TypeKind::LIST) {
        auto expected_list = std::dynamic_pointer_cast<ListType>(expected);
        auto actual_list = std::dynamic_pointer_cast<ListType>(actual);
        return check_type_compatibility(expected_list->element_type, actual_list->element_type);
    }
    // --- END OF FIXES ---

    return false;
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