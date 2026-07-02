#include "TypeChecker.h"
namespace angara {

    bool TypeChecker::check_structural_match(
        const std::shared_ptr<DataType>& data_type,
        const std::shared_ptr<RecordType>& record_type
    ) {
        for (const auto& [field_name, data_field_info] : data_type->fields) {
            auto record_field_it = record_type->fields.find(field_name);
            if (record_field_it == record_type->fields.end()) {
                return false;
            }

            if (!check_type_compatibility(data_field_info.type, record_field_it->second)) {
                return false;
            }
        }
        return true;
    }

bool TypeChecker::check_type_compatibility(
            const std::shared_ptr<Type>& expected,
            const std::shared_ptr<Type>& actual
) {
    if (expected->toString() == actual->toString()) return true;

    if (expected->kind == TypeKind::ANY || actual->kind == TypeKind::ANY) return true;

    if (expected->kind == TypeKind::TYPE_PARAM || actual->kind == TypeKind::TYPE_PARAM) return true;

    // Integer types are intercompatible — the marshaller handles truncation/extension
    if (isInteger(expected) && isInteger(actual)) return true;

    if (expected->kind == TypeKind::OPTIONAL) {
        auto optional_type = std::dynamic_pointer_cast<OptionalType>(expected);
        if (check_type_compatibility(optional_type->wrapped_type, actual) || actual->kind == TypeKind::NIL) {
            return true;
        }
    }

    if (expected->kind == TypeKind::DATA && actual->kind == TypeKind::RECORD) {
        return check_structural_match(
            std::dynamic_pointer_cast<DataType>(expected),
            std::dynamic_pointer_cast<RecordType>(actual)
        );
    }

    if (expected->kind == TypeKind::RECORD && actual->kind == TypeKind::RECORD) {
        auto expected_record = std::dynamic_pointer_cast<RecordType>(expected);
        auto actual_record = std::dynamic_pointer_cast<RecordType>(actual);

        if (expected_record->fields.empty()) {
            return true;
        }

        if (actual_record->fields.empty()) {
            return true;
        }
    }

    if (expected->kind == TypeKind::LIST && actual->kind == TypeKind::LIST) {
        auto expected_list = std::dynamic_pointer_cast<ListType>(expected);
        auto actual_list = std::dynamic_pointer_cast<ListType>(actual);
        return check_type_compatibility(expected_list->element_type, actual_list->element_type);
    }

    // Generic instance vs generic instance: same base type, compatible type args
    if (expected->kind == TypeKind::GENERIC_INSTANCE && actual->kind == TypeKind::GENERIC_INSTANCE) {
        auto expected_gen = std::dynamic_pointer_cast<GenericInstanceType>(expected);
        auto actual_gen = std::dynamic_pointer_cast<GenericInstanceType>(actual);
        if (expected_gen->base_type->toString() != actual_gen->base_type->toString()) return false;
        for (const auto& [name, expected_arg] : expected_gen->type_args) {
            auto it = actual_gen->type_args.find(name);
            if (it == actual_gen->type_args.end()) return false;
            if (!check_type_compatibility(expected_arg, it->second)) return false;
        }
        return true;
    }

    // Generic instance is compatible with its bare base data type
    if (expected->kind == TypeKind::DATA && actual->kind == TypeKind::GENERIC_INSTANCE) {
        auto expected_data = std::dynamic_pointer_cast<DataType>(expected);
        auto actual_gen = std::dynamic_pointer_cast<GenericInstanceType>(actual);
        return expected_data->toString() == actual_gen->base_type->toString();
    }

    // v5: ref<T> — implicit conversion from a tracked type to ref<T>.
    // A Buffer is assignable to a ref<Buffer> (taking a non-owning reference).
    if (expected->kind == TypeKind::REF) {
        auto ref_expected = std::dynamic_pointer_cast<RefType>(expected);
        return check_type_compatibility(ref_expected->inner_type, actual);
    }

    return false;
}

    void TypeChecker::visit(std::shared_ptr<const ReturnStmt> stmt) {
        if (m_function_return_types.empty()) {
            error(stmt->keyword, "'return' can only be used inside a function body.", "E264");
            return;
        }

        auto expected_return_type = m_function_return_types.top();

        if (stmt->value) {
            stmt->value->accept(*this);
            auto actual_return_type = popType();


            if (!check_type_compatibility(expected_return_type, actual_return_type)) {
                error(stmt->keyword, "Type mismatch. This function is declared to return '" +
                                     expected_return_type->toString() + "', but is returning a value of type '" +
                                     actual_return_type->toString() + "'.", "E265");
            }

        } else {
            if (!check_type_compatibility(expected_return_type, m_type_nil)) {
                error(stmt->keyword, "This function must return a value of type '" +
                                     expected_return_type->toString() + "'. An empty 'return;' is only valid for functions returning 'nil'.", "E266");
            }
        }
    }

}
