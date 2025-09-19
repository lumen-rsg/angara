//
// Created by cv2 on 9/19/25.
//
#include "CTranspiler.h"
namespace angara {

    std::string CTranspiler::transpileIsExpr(const IsExpr& expr) {
        std::string object_str = transpileExpr(expr.object);

        // Check if the type being checked is a generic `list`.
        if (auto generic_type = std::dynamic_pointer_cast<const GenericType>(expr.type)) {
            if (generic_type->name.lexeme == "list") {
                if (generic_type->arguments.size() == 1) {
                    // It is! Generate a call to our new, specialized function.
                    auto element_type_ast = generic_type->arguments[0];
                    if (auto simple_element_type = std::dynamic_pointer_cast<const SimpleType>(element_type_ast)) {
                        std::string element_type_name = simple_element_type->name.lexeme;
                        return "angara_is_list_of_type(" + object_str + ", \"" + element_type_name + "\")";
                    }
                }
            }
            // Fallback for other generics like 'record' if we add them later.
            return "angara_is_instance_of(" + object_str + ", \"" + generic_type->name.lexeme + "\")";
        }

        // Fallback for simple types (e.g., `is string`, `is Counter`).
        if (auto simple_type = std::dynamic_pointer_cast<const SimpleType>(expr.type)) {
            std::string type_name_str = simple_type->name.lexeme;
            return "angara_is_instance_of(" + object_str + ", \"" + type_name_str + "\")";
        }

        return "angara_create_bool(false)"; // Should be unreachable
    }

}