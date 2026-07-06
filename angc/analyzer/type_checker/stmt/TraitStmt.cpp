#include "TypeChecker.h"
namespace angara {

    void TypeChecker::visit(std::shared_ptr<const TraitStmt> stmt) {
        auto symbol = m_symbols.resolve(stmt->name.lexeme);
        auto trait_type = std::dynamic_pointer_cast<TraitType>(symbol->type);
        if (!trait_type) return;

        for (const auto& [method_name, method_stmt] : trait_type->default_bodies) {
            if (!method_stmt || !method_stmt->body) continue;

            auto it = trait_type->methods.find(method_name);
            if (it == trait_type->methods.end()) continue;
            auto method_type = it->second;

            // Reset per-function error state
            bool saved_had_error = m_hadError;
            m_hadError = false;
            m_is_in_trait = true;
            m_symbols.enterScope();

            m_function_return_types.push(method_type->return_type);

            // Declare parameters in scope
            for (size_t i = 0; i < method_stmt->params.size() && i < method_type->param_types.size(); ++i) {
                m_symbols.declare(method_stmt->params[i].name, method_type->param_types[i], true);
            }

            // Visit the body
            for (const auto& body_stmt : *(method_stmt->body)) {
                body_stmt->accept(*this, body_stmt);
            }

            m_function_return_types.pop();
            m_symbols.exitScope();
            m_is_in_trait = false;
            m_hadError = saved_had_error || m_hadError;
        }
    }

    void TypeChecker::defineTraitHeader(const TraitStmt& stmt) {
        auto symbol = m_symbols.resolve(stmt.name.lexeme);
        auto trait_type = std::dynamic_pointer_cast<TraitType>(symbol->type);

        if (stmt.is_exported) {
            m_module_type->exports[stmt.name.lexeme] = trait_type;
        }

        m_is_in_trait = true;

        for (const auto& method_stmt : stmt.methods) {
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
                error(method_stmt->name, "Duplicate method '" + method_stmt->name.lexeme + "' in trait '" + stmt.name.lexeme + "'.", "E307");
            } else {
                trait_type->methods[method_stmt->name.lexeme] = method_type;
                // TS-1/Phase D: retain a default body if the trait method has one.
                if (method_stmt->body) {
                    trait_type->default_bodies[method_stmt->name.lexeme] = method_stmt;
                }
            }
        }

        m_is_in_trait = false;
    }

}
