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
            std::string c_struct_name = "Angara_" + unwrapped_object_type->toString();
            // Cast the generic obj pointer to the specific data struct pointer and access the field.
            access_str = "((struct " + c_struct_name + "*)AS_OBJ(" + object_str + "))->" + sanitize_name(prop_name);
        }
        else if (unwrapped_object_type->kind == TypeKind::INSTANCE) {
            access_str = transpileGetExpr_on_instance(expr, object_str);
        }
        else if (unwrapped_object_type->kind == TypeKind::MODULE) {
            auto module_type = std::dynamic_pointer_cast<ModuleType>(unwrapped_object_type);
            // For a module, "accessing a property" means referring to the exported global variable.
            access_str = module_type->name + "_" + prop_name;
        } if (unwrapped_object_type->kind == TypeKind::ENUM) {
            // This is an access to an enum variant, like `Color.Green`.
            // We need to check if it's a nullary variant being used as a value,
            // or a constructor that is about to be called.

            // 1. Get the type of the ENTIRE GetExpr node itself (e.g., the type of `Color.Green`).
            auto get_expr_type = m_type_checker.m_expression_types.at(&expr);

            // 2. The TypeChecker resolves all variant accesses to a FunctionType.
            if (get_expr_type->kind == TypeKind::FUNCTION) {
                auto func_type = std::dynamic_pointer_cast<FunctionType>(get_expr_type);

                // 3. Check if it's a function with ZERO parameters.
                if (func_type->param_types.empty()) {
                    // This is a nullary variant being used as a value. We must transpile it
                    // to an immediate function call. e.g., `Angara_Color_Green()`
                    return "Angara_" + unwrapped_object_type->toString() + "_" + prop_name + "()";
                }
            }

            // If it's a function with parameters, it MUST be the callee of a CallExpr.
            // The CallExpr transpiler will add the parentheses. Just return the function name.
            // e.g., `Angara_WebEvent_KeyPress`
            return "Angara_" + unwrapped_object_type->toString() + "_" + prop_name;
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