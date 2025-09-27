//
// Created by cv2 on 9/19/25.
//

#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const AssignExpr& expr) {
        // 1. Determine the type of the value being assigned (RHS).
        expr.value->accept(*this);
        auto rhs_type = popType();


        if (auto subscript_target = std::dynamic_pointer_cast<const SubscriptExpr>(expr.target)) {
            // We need to get the types of the collection and index before proceeding.
            subscript_target->object->accept(*this);
            auto collection_type = popType();
            subscript_target->index->accept(*this);
            auto index_type = popType();

            if (collection_type->kind == TypeKind::ERROR || index_type->kind == TypeKind::ERROR) {
                pushAndSave(&expr, m_type_error); return {};
            }

            if (collection_type->kind == TypeKind::LIST) {
                auto list_type = std::dynamic_pointer_cast<ListType>(collection_type);
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
                    if (auto key_literal = std::dynamic_pointer_cast<const Literal>(subscript_target->index)) {
                        // STATIC ASSIGNMENT: Key is known.
                        auto record_type = std::dynamic_pointer_cast<RecordType>(collection_type);
                        auto field_it = record_type->fields.find(key_literal->token.lexeme);
                        if (field_it == record_type->fields.end()) {
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

        // --- Centralized Type Compatibility Check ---
        bool types_match = (lhs_type->toString() == rhs_type->toString());

        // Rule: A T or a `nil` can be assigned to a T?
        if (!types_match && lhs_type->kind == TypeKind::OPTIONAL) {
            auto optional_type = std::dynamic_pointer_cast<OptionalType>(lhs_type);
            if (optional_type->wrapped_type->toString() == rhs_type->toString() || rhs_type->kind == TypeKind::NIL) {
                types_match = true;
            }
        }

        // Rule: Anything can be assigned to `any`.
        if (!types_match && lhs_type->kind == TypeKind::ANY) {
            types_match = true;
        }

        // Rule: An `any` can be assigned to a typed variable (runtime cast assertion).
        if (!types_match && rhs_type->kind == TypeKind::ANY) {
            types_match = true;
        }

        // TODO ... (other special rules for empty list, etc.) ...

        // NEW RULE: Allow implicit narrowing for integer assignments.
        // e.g., allow `let x as i32; x = 0;` where 0 is an i64.
        if (!types_match && isInteger(lhs_type) && rhs_type->toString() == "i64") {
            // This is a potential narrowing conversion. For now, we allow it.
            // A more advanced compiler could issue a warning if the RHS is not a constant,
            // as it could lead to data loss at runtime. But allowing it makes the
            // language far more ergonomic.
            types_match = true;
        }

        if (!types_match) {
            error(expr.op, "Type mismatch. Cannot assign a value of type '" +
                           rhs_type->toString() + "' to a target of type '" +
                           lhs_type->toString() + "'.");
        }

        // 4. Check for type compatibility. The LHS type must match the RHS type.
        if (lhs_type->toString() != rhs_type->toString()) {
            // Special case for assigning an empty list `[]` to a typed list variable.
            if (auto list_expr = std::dynamic_pointer_cast<const ListExpr>(expr.value)) {

                if (list_expr->elements.empty() && lhs_type->kind == TypeKind::LIST) {
                    // This is valid, so we skip the error.
                } else {
                    error(expr.op, "Type mismatch. Cannot assign a value of type '" +
                                   rhs_type->toString() + "' to a target of type '" +
                                   lhs_type->toString() + "'.");
                }
            } else {
                error(expr.op, "Type mismatch. Cannot assign a value of type '" +
                               rhs_type->toString() + "' to a target of type '" +
                               lhs_type->toString() + "'.");

            }
        }

        // 5. Check for const-ness and other assignment rules based on the target's kind.
        if (auto var_target = std::dynamic_pointer_cast<const VarExpr>(expr.target)) {
            auto symbol = m_symbols.resolve(var_target->name.lexeme);
            if (symbol && symbol->is_const) {
                error(var_target->name, "Cannot assign to 'const' variable '" + symbol->name + "'.");
                note(symbol->declaration_token, "'" + symbol->name + "' was declared 'const' here.");
            }
        }

        else if (auto get_target = std::dynamic_pointer_cast<const GetExpr>(expr.target)) {
            // We need to re-evaluate the object type to get the ClassType.
            get_target->object->accept(*this);
            auto object_type = popType();

            if (object_type->kind == TypeKind::INSTANCE) {
                auto instance_type = std::dynamic_pointer_cast<InstanceType>(object_type);
                const std::string& field_name = get_target->name.lexeme;

                // Use our recursive helper to find the field in the inheritance chain.
                const ClassType::MemberInfo* field_info = instance_type->class_type->findProperty(field_name);

                if (field_info == nullptr) {
                    // The GetExpr visitor would have already caught this, but we check again for safety.
                    // Note: findProperty looks for methods too, we should only allow assigning to fields.
                    error(get_target->name, "Instance of class '" + instance_type->toString() +
                                            "' has no field named '" + field_name + "'.");
                } else {
                    // Check if the found property is actually a field.
                    if (instance_type->class_type->methods.count(field_name)) {
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