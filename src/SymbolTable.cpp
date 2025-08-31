//
// Created by cv2 on 8/31/25.
//

#include "SymbolTable.h"

namespace angara {

    SymbolTable::SymbolTable() {
        // When the symbol table is created, we always start with a single,
        // top-level "global" scope.
        enterScope();
    }

    void SymbolTable::enterScope() {
        // To enter a new scope, we just push a new, empty map onto our stack of scopes.
        m_scopes.emplace_back();
    }

    void SymbolTable::exitScope() {
        // To exit a scope, we pop the current map off the stack.
        // We should never be able to exit the bottom-most global scope.
        if (m_scopes.size() > 1) {
            m_scopes.pop_back();
        }
    }

    bool SymbolTable::declare(const Token& token, std::shared_ptr<Type> type, bool is_const) {
        auto& current_scope = m_scopes.back();
        if (current_scope.count(token.lexeme)) {
            return false;
        }
        auto symbol = std::make_shared<Symbol>();
        symbol->name = token.lexeme;
        symbol->type = std::move(type);
        symbol->is_const = is_const; // <-- Store the flag
        current_scope[token.lexeme] = std::move(symbol);
        return true;
    }

    std::shared_ptr<Symbol> SymbolTable::resolve(const std::string& name) const {
        // To resolve a variable, we walk the scope stack backwards, from the
        // innermost scope to the outermost (global) scope.

        for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
            const auto& scope = *it;
            auto symbol_it = scope.find(name);
            if (symbol_it != scope.end()) {
                // Found the symbol in this scope. Return it.
                return symbol_it->second;
            }
        }

        // If we've walked all the scopes and haven't found the name, it's undeclared.
        return nullptr;
    }

} // namespace angara