//
// Created by cv2 on 9/19/25.
//

#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const AssignExpr& expr) {
        // 1. Determine the type of the value being assigned (RHS).
        expr.value->accept(*this);
        const auto rhs_type = popType();


        if (const auto subscript_target = std::dynamic_pointer_cast<const SubscriptExpr>(expr.target)) {
            // We need to get the types of the collection and index before proceeding.
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
                    error(subscript_target->bracket, "List index for assignment must be an integer, but got '" + index_type->toString() + "'.");
                }
                if (list_type->element_type->toString() != rhs_type->toString()) {
                    error(expr.op, "Type mismatch. Cannot assign value of type '" + rhs_type->toString() + "' to an element of a list of type '" + list_type->toString() + "'.");
                }
            }
            else if (collection_type->kind == TypeKind::RECORD) {
                if (index_type->toString() != "string") {
                    error(subscript_target->bracket, "Record key for assignment must be a string, but got '" + index_type->toString() + "'.");
                } else {
                    if (const auto key_literal = std::dynamic_pointer_cast<const Literal>(subscript_target->index)) {
                        // STATIC ASSIGNMENT: Key is known.
                        const auto record_type = std::dynamic_pointer_cast<RecordType>(collection_type);
                        if (const auto field_it = record_type->fields.find(key_literal->token.lexeme); field_it == record_type->fields.end()) {
                            error(key_literal->token, "Record of type '" + record_type->toString() + "' has no statically-known field named '" + key_literal->token.lexeme + "'. Use a variable key to add a new field.");
                        } else if (field_it->second->toString() != rhs_type->toString()) {
                            error(expr.op, "Type mismatch. Cannot assign value of type '" + rhs_type->toString() + "' to field '" + key_literal->token.lexeme + "' of type '" + field_it->second->toString() + "'.");
                        }
                    }
                    // DYNAMIC ASSIGNMENT (key is a variable): This is allowed.
                }
            }

            // Subscript assignment expression evaluates to the RHS value.
            pushAndSave(&expr, rhs_type);
            return {};
        }

        // 2. Determine the type of the target being assigned to (LHS).
        expr.target->accept(*this);
        auto lhs_type = popType();

        // 3. If either sub-expression had an error, stop immediately.
        if (rhs_type->kind == TypeKind::ERROR || lhs_type->kind == TypeKind::ERROR) {
            pushAndSave(&expr, m_type_error);
            return {};
        }

        if (!check_type_compatibility(lhs_type, rhs_type)) {
            // We can add a special check here for integer literals if we want to be
            // even more robust, but the main logic is now centralized.
            bool types_match = false;
            if (isInteger(lhs_type) && rhs_type->toString() == "i64") {
                if (std::dynamic_pointer_cast<const Literal>(expr.value)) {
                    types_match = true;
                }
            }

            if (!types_match) {
                error(expr.op, "Type mismatch. Cannot assign a value of type '" +
                               rhs_type->toString() + "' to a target of type '" +
                               lhs_type->toString() + "'.");
            }
        }

        // 5. Check for const-ness and other assignment rules based on the target's kind.
        if (const auto var_target = std::dynamic_pointer_cast<const VarExpr>(expr.target)) {
            if (const auto symbol = m_symbols.resolve(var_target->name.lexeme); symbol && symbol->is_const) {
                error(var_target->name, "Cannot assign to 'const' variable '" + symbol->name + "'.");
                note(symbol->declaration_token, "'" + symbol->name + "' was declared 'const' here.");
            }
        }

        else if (const auto get_target = std::dynamic_pointer_cast<const GetExpr>(expr.target)) {
            // We need to re-evaluate the object type to get the ClassType.
            get_target->object->accept(*this);

            if (const auto object_type = popType(); object_type->kind == TypeKind::INSTANCE) {
                const auto instance_type = std::dynamic_pointer_cast<InstanceType>(object_type);
                const std::string& field_name = get_target->name.lexeme;

                // Use our recursive helper to find the field in the inheritance chain.

                if (const ClassType::MemberInfo* field_info = instance_type->class_type->findProperty(field_name); field_info == nullptr) {
                    // The GetExpr visitor would have already caught this, but we check again for safety.
                    // Note: findProperty looks for methods too, we should only allow assigning to fields.
                    error(get_target->name, "Instance of class '" + instance_type->toString() +
                                            "' has no field named '" + field_name + "'.");
                } else {
                    // Check if the found property is actually a field.
                    if (instance_type->class_type->methods.contains(field_name)) {
                        error(get_target->name, "Cannot assign to a method. '" + field_name + "' is a method, not a field.");
                    } else {
                        // It's a field. Now check if it's const.
                        if (field_info->is_const) {
                            error(get_target->name, "Cannot assign to 'const' field '" + field_name + "'.");
                        }
                    }
                }
            }
        }

        // An assignment expression evaluates to the assigned value.
        pushAndSave(&expr, rhs_type);
        return {};
    }

}