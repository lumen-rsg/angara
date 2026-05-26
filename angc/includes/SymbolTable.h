#pragma once

#include <string>
#include <map>
#include <vector>
#include <memory>
#include "Type.h"
#include "Token.h"

namespace angara {
    struct ModuleType;

    /// A single entry in the symbol table representing a declared name.
    struct Symbol {
        std::string name;
        std::shared_ptr<Type> type;
        Token declaration_token;
        bool is_const;
        int depth;
        bool used = false;
        std::shared_ptr<ModuleType> from_module = nullptr;
    };

    /// A scoped symbol table that maps names to their types and metadata.
    /// Maintains a stack of scopes; resolution walks from innermost to outermost.
    class SymbolTable {
    public:
        SymbolTable();

        /// Pushes a new empty scope onto the scope stack.
        void enterScope();

        /// Pops the current scope and returns all symbols that were declared but never used.
        /// The global scope (depth 0) is never popped.
        /// @return Vector of unused symbols from the exited scope.
        std::vector<std::shared_ptr<Symbol>> exitScope();

        /// Looks up a name by walking the scope stack from innermost to outermost.
        /// Marks the found symbol as used.
        /// @param name  The identifier to resolve.
        /// @return The matching symbol, or nullptr if not found in any scope.
        [[nodiscard]] std::shared_ptr<Symbol> resolve(const std::string& name);

        /// Declares a new symbol in the current (innermost) scope.
        /// @param token        The declaration token (used for error reporting).
        /// @param type         The symbol's resolved type.
        /// @param is_const     Whether the symbol is immutable.
        /// @param from_module  If imported, the origin module; nullptr for local declarations.
        /// @return The conflicting symbol if this name already exists in the current scope, or nullptr on success.
        std::shared_ptr<Symbol> declare(
            const Token &token,
            std::shared_ptr<Type> type,
            bool is_const,
            std::shared_ptr<ModuleType> from_module = nullptr
        );

        /// Returns the global scope (depth 0) as a read-only name-to-symbol map.
        [[nodiscard]] const std::map<std::string, std::shared_ptr<Symbol>>& getGlobalScope() const;

        /// Returns the current scope depth (0 = global).
        [[nodiscard]] int getScopeDepth() const;

        /// Returns the full scope stack for external iteration.
        [[nodiscard]] const std::vector<std::map<std::string, std::shared_ptr<Symbol>>>& getScopes() const;

    private:
        std::vector<std::map<std::string, std::shared_ptr<Symbol>>> m_scopes;
    };

}
