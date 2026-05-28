#include "TypeChecker.h"
namespace angara {

    void TypeChecker::defineEnumHeader(const EnumStmt& stmt) {
        auto symbol = m_symbols.resolve(stmt.name.lexeme);
        auto enum_type = std::dynamic_pointer_cast<EnumType>(symbol->type);

        if (stmt.is_exported) {
            m_module_type->exports[stmt.name.lexeme] = enum_type;
        }

        for (const auto& variant_node : stmt.variants) {
            const std::string& variant_name = variant_node->name.lexeme;

            if (enum_type->variants.count(variant_name)) {
                error(variant_node->name, "Duplicate variant '" + variant_name + "' in enum '" + enum_type->name + "'.", "E310");
                continue;
            }

            std::vector<std::shared_ptr<Type>> param_types;
            for (const auto& param_node : variant_node->params) {
                param_types.push_back(resolveType(param_node.type));
            }

            auto variant_constructor_type = std::make_shared<FunctionType>(
                param_types,
                enum_type
            );

            enum_type->variants[variant_name] = variant_constructor_type;
        }
    }

    void TypeChecker::visit(std::shared_ptr<const EnumStmt> stmt) {
    }

}
