#include "Parser.h"
namespace angara {

    // Grammar: func IDENTIFIER "(" (IDENTIFIER "as" type ...)? ")" ...
    std::shared_ptr<Stmt> Parser::function(const std::string& kind) {
        Token name = consume(TokenType::IDENTIFIER, "Expect " + kind + " name.");
        consume(TokenType::LEFT_PAREN, "Expect '(' after " + kind + " name.");

        // --- FIX: Methods implicitly have 'this' ---
        bool has_this = (kind == "method");
        // -------------------------------------------

        std::vector<Parameter> parameters;

        if (!check(TokenType::RIGHT_PAREN)) {
            // Check if the user explicit typed 'this' (optional now)
            if (match({TokenType::THIS})) {
                has_this = true; // explicit confirmation
                if (!check(TokenType::RIGHT_PAREN)) {
                    consume(TokenType::COMMA, "Expect ',' after 'this' parameter.");
                }
            }

            // Parse regular parameters
            if (!check(TokenType::RIGHT_PAREN)) {
                do {
                    Token param_name = consume(TokenType::IDENTIFIER, "Expect parameter name.");
                    consume(TokenType::AS, "Expect 'as' after parameter name.");
                    std::shared_ptr<ASTType> param_type = type();
                    bool is_variadic = match({TokenType::DOT_DOT_DOT});

                    parameters.push_back({param_name, param_type, is_variadic}); // Ensure your struct has is_variadic

                    if (is_variadic && !check(TokenType::RIGHT_PAREN)) {
                        throw error(peek(), "A variadic parameter '...' must be the last parameter.");
                    }
                } while (match({TokenType::COMMA}));
            }
        }

        consume(TokenType::RIGHT_PAREN, "Expect ')' after parameters.");

        // ... (Rest of the function remains the same) ...
        std::shared_ptr<ASTType> returnType = nullptr;
        if (match({TokenType::MINUS_GREATER})) {
            returnType = type();
        }

        std::optional<std::vector<std::shared_ptr<Stmt>>> body = std::nullopt;
        if (match({TokenType::LEFT_BRACE})) {
            body = block();
        } else if (match({TokenType::SEMICOLON})) {
            // No body
        } else {
            throw error(peek(), "Expect '{' to start a function body or ';' for an interface declaration.");
        }

        return std::make_shared<FuncStmt>(std::move(name), has_this, std::move(parameters), returnType, body);
    }
}