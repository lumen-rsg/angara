#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const AssignExpr& expr) {
        expr.value->accept(*this);
        const auto rhs_type = popType();


        if (const auto subscript_target = std::dynamic_pointer_cast<const SubscriptExpr>(expr.target)) {
            subscript_target->object->accept(*this);
            const auto collection_type = popType();
            subscript_target->index->accept(*this);
            const auto index_type = popType();

            if (collection_type->kind == TypeKind::ERROR || index_type->kind == TypeKind::ERROR) {
                pushAndSave(&expr, m_type_error); return {};
            }

            if (collection_type->kind == TypeKind::LIST) {
                const auto list_type = std::dynamic_pointer_cast<ListType>(collection_type);
                if (!isInteger(index_type)) {
                    error(subscript_target->bracket, "List index must be an integer, but got '" + index_type->toString() + "'.");
                }
                if (list_type->element_type->toString() != rhs_type->toString()) {
                    error(expr.op, "Cannot assign a value of type '" + rhs_type->toString() + "' to a list element of type '" + list_type->element_type->toString() + "'.");
                }
            }
            else if (collection_type->kind == TypeKind::RECORD) {
                auto record_type = std::dynamic_pointer_cast<RecordType>(collection_type);
                if (index_type->toString() != "string") {
                    error(subscript_target->bracket, "Record key for assignment must be a string, but got '" + index_type->toString() + "'.");
                } else {
                    if (record_type->fields.empty()) {
                        // Dynamic field addition on generic record — always valid.
                    } else {
                        if (auto key_literal = std::dynamic_pointer_cast<const Literal>(subscript_target->index)) {
                            auto field_it = record_type->fields.find(key_literal->token.lexeme);
                            if (field_it == record_type->fields.end()) {
                                error(key_literal->token, "Record has no field named '" + key_literal->token.lexeme + "'.");
                            } else if (!check_type_compatibility(field_it->second, rhs_type)) {
                                error(expr.op, "Cannot assign a value of type '" + rhs_type->toString() + "' to field '" + key_literal->token.lexeme + "' which expects type '" + field_it->second->toString() + "'.");
                            }
                        }
                    }
                }
            }
            pushAndSave(&expr, rhs_type);
            return {};
        }

        expr.target->accept(*this);
        auto lhs_type = popType();

        if (rhs_type->kind == TypeKind::ERROR || lhs_type->kind == TypeKind::ERROR) {
            pushAndSave(&expr, m_type_error);
            return {};
        }

        if (!check_type_compatibility(lhs_type, rhs_type)) {
            bool types_match = false;
            if (isInteger(lhs_type) && rhs_type->toString() == "i64") {
                if (std::dynamic_pointer_cast<const Literal>(expr.value)) {
                    types_match = true;
                }
            }
            if (m_is_in_unsafe_context && rhs_type->kind == TypeKind::ANY) {
                types_match = true;
            }

            if (!types_match) {
                error(expr.op, "Type mismatch. Cannot assign a value of type '" +
                               rhs_type->toString() + "' to a target of type '" +
                               lhs_type->toString() + "'.");
            }
        }

        if (const auto var_target = std::dynamic_pointer_cast<const VarExpr>(expr.target)) {
            if (const auto symbol = m_symbols.resolve(var_target->name.lexeme); symbol && symbol->is_const) {
                error(var_target->name, "Cannot assign to 'const' variable '" + symbol->name + "'.");
                note(symbol->declaration_token, "'" + symbol->name + "' was declared 'const' here.");
            }
        }

        else if (const auto get_target = std::dynamic_pointer_cast<const GetExpr>(expr.target)) {
            get_target->object->accept(*this);

            if (const auto object_type = popType(); object_type->kind == TypeKind::INSTANCE) {
                const auto instance_type = std::dynamic_pointer_cast<InstanceType>(object_type);
                const std::string& field_name = get_target->name.lexeme;

                if (const ClassType::MemberInfo* field_info = instance_type->class_type->findProperty(field_name); field_info == nullptr) {
                    error(get_target->name, "Class '" + instance_type->toString() +
                                            "' has no field named '" + field_name + "'.");
                } else {
                    if (instance_type->class_type->methods.contains(field_name)) {
                        error(get_target->name, "Cannot assign to method '" + field_name + "' — methods are not assignable.");
                    } else {
                        if (field_info->is_const) {
                            error(get_target->name, "Cannot assign to 'const' field '" + field_name + "'.");
                        }
                    }
                }
            }
        }

        pushAndSave(&expr, rhs_type);
        return {};
    }

}
