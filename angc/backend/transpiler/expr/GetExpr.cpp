//
// Created by cv2 on 9/19/25.
//
#include "CTranspiler.h"
namespace angara{

    std::string CTranspiler::transpileGetExpr(const GetExpr& expr) {
        // 1. Transpile the object on the left of the operator.
        std::string object_str = transpileExpr(expr.object);
        const std::string& prop_name = expr.name.lexeme;

        // 2. Get the pre-computed type of the object from the Type Checker.
        auto object_type = m_type_checker.m_expression_types.at(expr.object.get());

        // 3. Determine the actual type, peeling off one layer of optionality if it exists.
        auto unwrapped_object_type = (object_type->kind == TypeKind::OPTIONAL)
            ? std::dynamic_pointer_cast<OptionalType>(object_type)->wrapped_type
            : object_type;

        // 4. Generate the raw C code for the property access itself.
        std::string access_str = "/* <invalid_get_expr> */";

        if (unwrapped_object_type->kind == TypeKind::DATA) {
            auto data_type = std::dynamic_pointer_cast<DataType>(unwrapped_object_type);

            // --- NEW, SIMPLIFIED LOGIC ---
            // Check the flag directly on the semantic Type object. No AST search needed.
            if (data_type->is_foreign) {
                auto field_info_it = data_type->fields.find(prop_name);
                if (field_info_it != data_type->fields.end()) {
                    auto field_type = field_info_it->second.type;
                    std::string c_struct_name = "Angara_" + data_type->name;

                    // The access path: (wrapper_struct*)->ptr->field_name
                    std::string raw_access = "((struct " + c_struct_name + "*)AS_OBJ(" + object_str + "))->ptr->" + prop_name;

                    // Box the raw C value back into an AngaraObject.
                    std::string boxing_func = "angara_from_c_" + field_type->toString();

                    // Handle optional chaining
                    if (expr.op.type == TokenType::QUESTION_DOT || object_type->kind == TypeKind::OPTIONAL) {
                        return "(IS_NIL(" + object_str + ") ? angara_create_nil() : " + boxing_func + "(" + raw_access + "))";
                    }
                    return boxing_func + "(" + raw_access + ")";
                }
            }
            // --- END OF NEW LOGIC ---

            // If it's not a foreign data type, it must be a regular one.
            // Fall through to the original logic for regular Angara 'data' field access.
            std::string c_struct_name = "Angara_" + unwrapped_object_type->toString();
            return "((struct " + c_struct_name + "*)AS_OBJ(" + object_str + "))->" + sanitize_name(prop_name);
        }
        else if (unwrapped_object_type->kind == TypeKind::INSTANCE) {
            access_str = transpileGetExpr_on_instance(expr, object_str);
        }
        else if (unwrapped_object_type->kind == TypeKind::MODULE) {
            auto module_type = std::dynamic_pointer_cast<ModuleType>(unwrapped_object_type);
            // For a module, "accessing a property" means referring to the exported global variable.
            access_str = module_type->name + "_" + prop_name;
        } else if (unwrapped_object_type->kind == TypeKind::ENUM) {
            auto enum_type = std::dynamic_pointer_cast<EnumType>(unwrapped_object_type);

            // Look up the *constructor signature* for the variant from the EnumType definition.
            // This is the source of truth.
            auto variant_it = enum_type->variants.find(prop_name);

            // This should always succeed if the Type Checker did its job.
            if (variant_it != enum_type->variants.end()) {
                auto variant_constructor_type = variant_it->second;

                // If the constructor function takes no parameters, it's a nullary variant.
                // When used as a value, we must transpile it to an immediate C function call.
                if (variant_constructor_type->param_types.empty()) {
                    return "Angara_" + enum_type->name + "_" + prop_name + "()";
                }
            }

            // If the variant is not nullary (it takes parameters), then it must be
            // part of a CallExpr. Just return the C function name, and the
            // CallExpr transpiler will add the parentheses and arguments.
            return "Angara_" + enum_type->name + "_" + prop_name;
        }
        else if (unwrapped_object_type->kind == TypeKind::EXCEPTION) {
            if (prop_name == "message") {
                // The C struct is AngaraException, and its field is `message`.
                access_str = "((AngaraException*)AS_OBJ(" + object_str + "))->message";
            } else {
                access_str = "/* <invalid_exception_field> */";
            }
        }

        // 5. If the access was optional, wrap the raw access in a nil-check.
        // An access is considered optional if the `?.` operator was used, OR if the
        // object being accessed was an optional type to begin with.
        if (expr.op.type == TokenType::QUESTION_DOT || object_type->kind == TypeKind::OPTIONAL) {
            // Generates the C ternary: (IS_NIL(obj) ? create_nil() : <the_actual_access>)
            return "(IS_NIL(" + object_str + ") ? angara_create_nil() : " + access_str + ")";
        }

        // 6. If it was a regular access, return the raw access string.
        return access_str;
    }

    std::string CTranspiler::transpileGetExpr_on_instance(const GetExpr& expr, const std::string& object_str) {
        const std::string& prop_name = expr.name.lexeme;
        auto object_type = m_type_checker.m_expression_types.at(expr.object.get());

        if (object_type->kind == TypeKind::OPTIONAL) {
            object_type = std::dynamic_pointer_cast<OptionalType>(object_type)->wrapped_type;
        }
        auto instance_type = std::dynamic_pointer_cast<InstanceType>(object_type);

        // 1. Find which class in the hierarchy actually defines this property.
        const ClassType* owner_class = findPropertyOwner(instance_type->class_type.get(), prop_name);
        if (!owner_class) {
            return "/* <unknown_property> */";
        }

        // 2. Build the access path by traversing the `parent` members.
        std::string access_path = "->";
        const ClassType* current = instance_type->class_type.get();
        while (current && current->name != owner_class->name) {
            // Prepend `parent.` for each level of inheritance we go up.
            access_path += "parent.";
            current = current->superclass.get();
        }
        access_path += sanitize_name(prop_name);

        // 3. The initial cast is to the struct of the INSTANCE being accessed.
        std::string base_struct_name = "Angara_" + instance_type->class_type->name;

        return "((" + base_struct_name + "*)AS_OBJ(" + object_str + "))" + access_path;
    }

}