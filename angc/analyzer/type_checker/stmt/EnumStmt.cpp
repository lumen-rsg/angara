#include "TypeChecker.h"
namespace angara {

    void TypeChecker::defineEnumHeader(const EnumStmt& stmt) {
        auto symbol = m_symbols.resolve(stmt.name.lexeme);
        auto enum_type = std::dynamic_pointer_cast<EnumType>(symbol->type);

        // LANG-8: store type parameter names on the EnumType
        for (const auto& tp : stmt.type_params) {
            enum_type->type_params.push_back(tp.lexeme);
        }

        // LANG-8: resolve type-param bounds (<T: Trait>) to their TraitType.
        for (const auto& [param_name, bound_token] : stmt.type_param_bounds) {
            auto bound_symbol = m_symbols.resolve(bound_token.lexeme);
            if (!bound_symbol) {
                error(bound_token, "Trait '" + bound_token.lexeme + "' used as a bound is not defined.", "E389");
            } else if (bound_symbol->type->kind != TypeKind::TRAIT) {
                error(bound_token, "'" + bound_token.lexeme + "' is not a trait and cannot be used as a bound.", "E390");
            } else {
                enum_type->type_param_bounds[param_name] =
                    std::dynamic_pointer_cast<TraitType>(bound_symbol->type);
            }
        }

        // LANG-8: install type params so variant payload types can reference them
        auto saved_type_params = m_active_type_params;
        for (const auto& tp : stmt.type_params) {
            m_active_type_params[tp.lexeme] = std::make_shared<TypeParameterType>(tp.lexeme);
        }

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

        // LANG-8: restore saved type params
        m_active_type_params = saved_type_params;
    }

    void TypeChecker::visit(std::shared_ptr<const EnumStmt> stmt) {
    }

}
