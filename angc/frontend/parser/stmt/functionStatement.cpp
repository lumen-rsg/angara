//
// Created by cv2 on 9/19/25.
//

#include "Parser.h"
namespace angara {

    // Grammar: func IDENTIFIER "(" (IDENTIFIER "as" type ("," IDENTIFIER "as" type)*)? ")" ( "->" type )? "{" block "}"
    std::shared_ptr<Stmt> Parser::function(const std::string& kind) {
        Token name = consume(TokenType::IDENTIFIER, "Expect " + kind + " name.");
        consume(TokenType::LEFT_PAREN, "Expect '(' after " + kind + " name.");

        bool has_this = false;
        std::vector<Parameter> parameters;

        if (!check(TokenType::RIGHT_PAREN)) {
            // The parameter list is not empty.

            // Check if the VERY FIRST parameter is 'this'.
            if (match({TokenType::THIS})) {
                has_this = true;
                // If there's more after 'this', there must be a comma.
                if (!check(TokenType::RIGHT_PAREN)) {
                    consume(TokenType::COMMA, "Expect ',' after 'this' parameter.");
                }
            }

            // Now, parse a comma-separated list of zero-or-more REGULAR parameters.
            // This 'if' condition will be true if:
            //  a) The first token was not 'this'.
            //  b) The first token WAS 'this', and it was followed by a comma.
            if (!check(TokenType::RIGHT_PAREN)) {
                do {
                    Token param_name = consume(TokenType::IDENTIFIER, "Expect parameter name.");
                    consume(TokenType::AS, "Expect 'as' after parameter name.");
                    std::shared_ptr<ASTType> param_type = type();
                    bool is_variadic = match({TokenType::DOT_DOT_DOT});

                    parameters.push_back({param_name, param_type});

                    // Rule: The variadic parameter must be the last one.
                    if (is_variadic && !check(TokenType::RIGHT_PAREN)) {
                        throw error(peek(), "A variadic parameter '...' must be the last parameter in a function signature.");
                    }
                } while (match({TokenType::COMMA}));
            }
        }

        consume(TokenType::RIGHT_PAREN, "Expect ')' after parameters.");

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