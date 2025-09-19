//
// Created by cv2 on 9/19/25.
//

#include "TypeChecker.h"
namespace angara {

    void TypeChecker::defineEnumHeader(const EnumStmt& stmt) {
        // 1. Get the placeholder EnumType we created in Pass 1.
        auto symbol = m_symbols.resolve(stmt.name.lexeme);
        auto enum_type = std::dynamic_pointer_cast<EnumType>(symbol->type);

        if (stmt.is_exported) {
            m_module_type->exports[stmt.name.lexeme] = enum_type;
        }

        // 2. Iterate through the AST variants and create semantic EnumVariantTypes.
        for (const auto& variant_node : stmt.variants) {
            const std::string& variant_name = variant_node->name.lexeme;

            if (enum_type->variants.count(variant_name)) {
                error(variant_node->name, "Duplicate variant name '" + variant_name + "' in enum '" + enum_type->name + "'.");
                continue;
            }

            // Resolve the types of the variant's parameters.
            std::vector<std::shared_ptr<Type>> param_types;
            for (const auto& param_node : variant_node->params) {
                param_types.push_back(resolveType(param_node.type));
            }

            // Create the semantic type for this specific variant.
            auto variant_constructor_type = std::make_shared<FunctionType>(
                param_types,
                enum_type // The return type is the EnumType itself
            );

            // Store it in the parent enum's map.
            enum_type->variants[variant_name] = variant_constructor_type;
        }
    }

    void TypeChecker::visit(std::shared_ptr<const EnumStmt> stmt) {
        // All semantic validation was done in `defineEnumHeader`.
        // There is no executable code to check in an enum definition itself.
    }

}