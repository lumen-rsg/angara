//
// Created by cv2 on 8/31/25.
//

#pragma once

#include <string>
#include <map>
#include <vector>
#include <memory>
#include "Type.h"
#include "Token.h"

namespace angara {

    // Represents a single entry in the symbol table (a declared variable)
    struct Symbol {
        std::string name;
        std::shared_ptr<Type> type;
        Token declaration_token;
        bool is_const;
        int depth;
    };

    class SymbolTable {
    public:
        SymbolTable();

        // Enters a new scope (e.g., on entering a '{' block)
        void enterScope();

        // Exits the current scope
        void exitScope();

        // Tries to find a symbol by walking up the scope chain.
        // Returns the symbol if found, otherwise nullptr.
        [[nodiscard]] std::shared_ptr<Symbol> resolve(const std::string& name) const;

        [[nodiscard]] std::shared_ptr<Symbol> declare(const Token &token, std::shared_ptr<Type> type, bool is_const);
        const std::map<std::string, std::shared_ptr<Symbol>>& getGlobalScope() const;
        int getScopeDepth() const;


    private:
        // A stack of scopes, where each scope is a map from name to Symbol.
        std::vector<std::map<std::string, std::shared_ptr<Symbol>>> m_scopes;

    };

} // namespace angara
