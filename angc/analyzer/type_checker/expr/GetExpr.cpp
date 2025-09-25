//
// Created by cv2 on 9/19/25.
//
#include "TypeChecker.h"
namespace angara {

std::any TypeChecker::visit(const GetExpr& expr) {
    // 1. First, recursively type check the object on the left of the operator.
    expr.object->accept(*this);
    auto object_type = popType();

    // Bail out early if the object itself had a type error.
    if (object_type->kind == TypeKind::ERROR) {
        pushAndSave(&expr, m_type_error);
        return {};
    }

    // 2. Determine if this is an optional chain (`?.`) or a regular access (`.`).
    bool is_optional_chain = (expr.op.type == TokenType::QUESTION_DOT);
    std::shared_ptr<Type> unwrapped_object_type = object_type;

    // 3. Handle the optionality of the object.
    if (object_type->kind == TypeKind::OPTIONAL) {
        // The object is optional (e.g., `Player?`). We can proceed with either `.` or `?.`.
        // We will work with the type it wraps (e.g., `Player`).
        unwrapped_object_type = std::dynamic_pointer_cast<OptionalType>(object_type)->wrapped_type;
    } else if (is_optional_chain) {
        // This is the case `non_optional?.field`. It's redundant but safe.
        // TODO: Add a compiler warning/note here for good style.
    }

    // A regular access (`.`) on an optional type is a compile-time error.
    // This prevents accidental null pointer errors.
    if (object_type->kind == TypeKind::OPTIONAL && !is_optional_chain) {
        error(expr.op, "Cannot access property on an optional type '" + object_type->toString() + "'. Use the optional chaining operator '?.' instead.");
        pushAndSave(&expr, m_type_error);
        return {};
    }

    // 4. Find the property on the (now unwrapped) type.
    const std::string& property_name = expr.name.lexeme;
    std::shared_ptr<Type> property_type = m_type_error; // Default to error

    // --- Dispatch based on the kind of the unwrapped type ---

    if (unwrapped_object_type->kind == TypeKind::DATA) {
        auto data_type = std::dynamic_pointer_cast<DataType>(unwrapped_object_type);
        auto field_it = data_type->fields.find(property_name);
        if (field_it == data_type->fields.end()) {
            error(expr.name, "Data block of type '" + data_type->name + "' has no field named '" + property_name + "'.");
        } else {
            property_type = field_it->second.type;
        }
    }
    else if (unwrapped_object_type->kind == TypeKind::INSTANCE) {
        auto instance_type = std::dynamic_pointer_cast<InstanceType>(unwrapped_object_type);
        const ClassType::MemberInfo* prop_info = instance_type->class_type->findProperty(property_name);
        if (!prop_info) {
            error(expr.name, "Instance of class '" + instance_type->toString() + "' has no property named '" + property_name + "'.");

            std::vector<std::string> candidates;
            // This is a simplification; a full implementation would walk the superclass chain. // TODO
            for(const auto& [name, member] : instance_type->class_type->fields) candidates.push_back(name);
            for(const auto& [name, member] : instance_type->class_type->methods) candidates.push_back(name);
            find_and_report_suggestion(expr.name, candidates);
        } else {
            // Check for private access
            if (prop_info->access == AccessLevel::PRIVATE && (m_current_class == nullptr || m_current_class->name != instance_type->class_type->name)) {
                error(expr.name, "Property '" + property_name + "' is private and cannot be accessed from this context.");
            } else {
                property_type = prop_info->type;
            }
        }
    }
    else if (unwrapped_object_type->kind == TypeKind::ENUM) {
        auto enum_type = std::dynamic_pointer_cast<EnumType>(unwrapped_object_type);
        auto variant_it = enum_type->variants.find(property_name);

        if (variant_it == enum_type->variants.end()) {
            error(expr.name, "Enum '" + enum_type->name + "' has no variant named '" + property_name + "'.");
        } else {
            auto variant_constructor_type = std::dynamic_pointer_cast<FunctionType>(variant_it->second);

            // If a variant takes no arguments (is nullary), accessing it directly
            // (e.g., `WebEvent.PageLoad`) immediately produces an instance of the enum.
            if (variant_constructor_type->param_types.empty()) {
                property_type = variant_constructor_type->return_type; // This is the EnumType itself
            } else {
                // If it takes arguments, accessing it returns the constructor function,
                // which must then be called.
                property_type = variant_constructor_type;
            }
        }
    }
    else if (unwrapped_object_type->kind == TypeKind::MODULE) {
        auto module_type = std::dynamic_pointer_cast<ModuleType>(unwrapped_object_type);
        auto member_it = module_type->exports.find(property_name);
        if (member_it == module_type->exports.end()) {
            error(expr.name, "Module '" + module_type->name + "' has no exported member named '" + property_name + "'.");

            std::vector<std::string> candidates;
            for(const auto& [name, type] : module_type->exports) candidates.push_back(name);
            find_and_report_suggestion(expr.name, candidates);

        } else {
            property_type = member_it->second;
        }
    }
    else if (unwrapped_object_type->kind == TypeKind::MODULE) {
        auto module_type = std::dynamic_pointer_cast<ModuleType>(unwrapped_object_type);
        auto member_it = module_type->exports.find(property_name);
        if (member_it != module_type->exports.end()) {
            property_type = member_it->second;
            if (module_type->is_native) {
                m_used_native_symbols.insert({module_type, property_name, property_type});
            }
        }
    }
    else if (unwrapped_object_type->kind == TypeKind::LIST) {
        auto list_type = std::dynamic_pointer_cast<ListType>(unwrapped_object_type);
        if (property_name == "push") {
            property_type = std::make_shared<FunctionType>(
                std::vector<std::shared_ptr<Type>>{list_type->element_type},
                m_type_nil
            );
        } else if (property_name == "remove_at") { // <-- ADD THIS
            property_type = std::make_shared<FunctionType>(
                std::vector<std::shared_ptr<Type>>{m_type_i64},
                list_type->element_type // Returns the element type
            );
        } else if (property_name == "remove") { // <-- ADD THIS
            property_type = std::make_shared<FunctionType>(
               std::vector<std::shared_ptr<Type>>{list_type->element_type},
               m_type_bool // Returns true or false
           );
        } else {
            error(expr.name, "Type 'list' has no property named '" + property_name + "'.");
        }
    }
    else if (unwrapped_object_type->kind == TypeKind::RECORD) {
        if (property_name == "remove") {
            property_type = std::make_shared<FunctionType>(
                std::vector<std::shared_ptr<Type>>{m_type_string},
                m_type_bool // Returns true or false
            );
        } else if (property_name == "keys") {
            auto list_of_strings = std::make_shared<ListType>(m_type_string);
            property_type = std::make_shared<FunctionType>(
                std::vector<std::shared_ptr<Type>>{},
                list_of_strings // Returns list<string>
            );
        } else {
            error(expr.name, "Type 'record' has no property named '" + property_name + "'. Use subscript `[]` to access fields.");
        }
    }
    else if (unwrapped_object_type->kind == TypeKind::THREAD) {
        if (property_name == "join") {
            property_type = std::make_shared<FunctionType>(std::vector<std::shared_ptr<Type>>{}, m_type_any);
        } else {
            error(expr.name, "Type 'Thread' has no property named '" + property_name + "'.");
        }
    }
    else if (unwrapped_object_type->kind == TypeKind::MUTEX) {
        if (property_name == "lock" || property_name == "unlock") {
            property_type = std::make_shared<FunctionType>(std::vector<std::shared_ptr<Type>>{}, m_type_nil);
        } else {
            error(expr.name, "Type 'Mutex' has no property named '" + property_name + "'.");
        }
    }
    else {
        error(expr.op, "Type '" + object_type->toString() + "' has no properties that can be accessed.");
    }

    // --- 5. Determine the Final Result Type ---
    if (property_type->kind == TypeKind::ERROR) {
        // If the property lookup failed, the result is an error.
        pushAndSave(&expr, m_type_error);
    } else if (is_optional_chain || object_type->kind == TypeKind::OPTIONAL) {
        // If this was an optional chain OR if the original object was optional,
        // the result of the access is also optional.
        pushAndSave(&expr, std::make_shared<OptionalType>(property_type));
    } else {
        // Otherwise, it's a regular access on a non-optional type.
        pushAndSave(&expr, property_type);
    }

    return {};
}

}