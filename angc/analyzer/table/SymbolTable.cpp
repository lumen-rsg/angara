#include "SymbolTable.h"
#include <cassert>

namespace angara {

    SymbolTable::SymbolTable() {
        enterScope();
    }

    void SymbolTable::enterScope() {
        m_scopes.emplace_back();
    }

    std::vector<std::shared_ptr<Symbol>> SymbolTable::exitScope() {
        std::vector<std::shared_ptr<Symbol>> unused;
        if (m_scopes.size() > 1) {
            auto& scope = m_scopes.back();
            for (const auto& [name, sym] : scope) {
                if (!sym->used) {
                    unused.push_back(sym);
                }
            }
            m_scopes.pop_back();
        }
        return unused;
    }

    std::shared_ptr<Symbol> SymbolTable::declare(
        const Token &token,
        std::shared_ptr<Type> type,
        bool is_const,
        std::shared_ptr<ModuleType> from_module
    ) {
        auto& current_scope = m_scopes.back();
        auto it = current_scope.find(token.lexeme);
        if (it != current_scope.end()) {
            return it->second;
        }

        auto symbol = std::make_shared<Symbol>();
        symbol->name = token.lexeme;
        symbol->type = std::move(type);
        symbol->declaration_token = token;
        symbol->is_const = is_const;
        symbol->depth = getScopeDepth();
        symbol->from_module = std::move(from_module);

        current_scope[token.lexeme] = symbol;
        return nullptr;
    }

    std::shared_ptr<Symbol> SymbolTable::resolve(const std::string& name) {
        for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
            const auto& scope = *it;
            auto symbol_it = scope.find(name);
            if (symbol_it != scope.end()) {
                symbol_it->second->used = true;
                return symbol_it->second;
            }
        }
        return nullptr;
    }

    const std::map<std::string, std::shared_ptr<Symbol>>& SymbolTable::getGlobalScope() const {
        // M12: the constructor guarantees m_scopes is never empty (it calls
        // enterScope()). A moved-from or otherwise corrupted table could violate
        // that; front() on an empty vector is UB. Assert so corruption surfaces
        // as a clear failure instead of silently indexing into garbage.
        assert(!m_scopes.empty() && "SymbolTable::m_scopes must never be empty");
        return m_scopes.front();
    }

    int SymbolTable::getScopeDepth() const {
        return m_scopes.size() - 1;
    }

    const std::vector<std::map<std::string, std::shared_ptr<Symbol>>>& SymbolTable::getScopes() const { return m_scopes; }

    std::shared_ptr<Symbol> SymbolTable::findShadowed(const std::string& name) const {
        // Walk scopes from second-innermost to outermost, looking for the name.
        if (m_scopes.size() < 2) return nullptr;
        for (auto it = m_scopes.rbegin() + 1; it != m_scopes.rend(); ++it) {
            auto symbol_it = it->find(name);
            if (symbol_it != it->end()) {
                return symbol_it->second;
            }
        }
        return nullptr;
    }

}
