//
// Created by cv2 on 9/19/25.
//

#include "TypeChecker.h"
namespace angara {

    void TypeChecker::visit(std::shared_ptr<const TraitStmt> stmt) {
        // handled in the passes.
    }

    void TypeChecker::defineTraitHeader(const TraitStmt& stmt) {
        auto symbol = m_symbols.resolve(stmt.name.lexeme);
        auto trait_type = std::dynamic_pointer_cast<TraitType>(symbol->type);

        // 2. If the trait was marked with 'export', add it to the module's public API.
        if (stmt.is_exported) {
            m_module_type->exports[stmt.name.lexeme] = trait_type;
        }

        // We are defining a trait's header, so we ARE in a trait.
        m_is_in_trait = true;

        for (const auto& method_stmt : stmt.methods) {
            // We need to resolve the signature of this method prototype.
            // We can't use defineFunctionHeader because it declares a global symbol.
            // Let's do it manually.
            std::vector<std::shared_ptr<Type>> param_types;
            for (const auto& p : method_stmt->params) {
                param_types.push_back(resolveType(p.type));
            }
            std::shared_ptr<Type> return_type = m_type_nil;
            if (method_stmt->returnType) {
                return_type = resolveType(method_stmt->returnType);
            }
            auto method_type = std::make_shared<FunctionType>(param_types, return_type);

            if (trait_type->methods.count(method_stmt->name.lexeme)) {
                error(method_stmt->name, "Duplicate method in trait.");
            } else {
                trait_type->methods[method_stmt->name.lexeme] = method_type;
            }
        }

        m_is_in_trait = false;
    }

}