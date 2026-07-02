#include "Parser.h"
namespace angara {

    std::shared_ptr<Stmt> Parser::function(const std::string& kind) {
        Token name = consume(TokenType::IDENTIFIER, "Expected " + kind + " name.", "E187");
        std::map<std::string, Token> type_param_bounds;
        auto type_params = parseTypeParams(type_param_bounds);

        consume(TokenType::LEFT_PAREN, "Expected '(' after " + kind + " name.", "E188");
        bool has_this = (kind == "method");

        std::vector<Parameter> parameters;

        if (!check(TokenType::RIGHT_PAREN)) {
            if (match({TokenType::THIS})) {
                has_this = true;
                if (!check(TokenType::RIGHT_PAREN)) {
                    consume(TokenType::COMMA, "Expected ',' after 'this' parameter.", "E189");
                }
            }

            if (!check(TokenType::RIGHT_PAREN)) {
                do {
                    if (check(TokenType::RIGHT_PAREN)) break;
                    Token param_name = consume(TokenType::IDENTIFIER, "Expected parameter name.", "E190");
                    consume(TokenType::AS, "Expected 'as' followed by a type after parameter name.", "E191");
                    std::shared_ptr<ASTType> param_type = type();
                    bool is_variadic = match({TokenType::DOT_DOT_DOT});

                    parameters.push_back({param_name, param_type, is_variadic});

                    if (is_variadic && !check(TokenType::RIGHT_PAREN)) {
                        throw error(peek(), "Variadic parameter '...' must be the last parameter — no further parameters are allowed after it.", "E192");
                    }
                } while (match({TokenType::COMMA}));
            }
        }

        consume(TokenType::RIGHT_PAREN, "Expected ')' after parameter list.", "E193");

        std::shared_ptr<ASTType> returnType = nullptr;
        if (match({TokenType::MINUS_GREATER})) {
            returnType = type();
        }

        std::optional<std::vector<std::shared_ptr<Stmt>>> body = std::nullopt;
        if (match({TokenType::LEFT_BRACE})) {
            body = block();
        } else if (match({TokenType::SEMICOLON})) {
        } else {
            throw error(peek(), "Expected '{' for the " + kind + " body, or ';' for a declaration without a body.", "E194");
        }

        return std::make_shared<FuncStmt>(std::move(name), has_this, std::move(parameters), returnType, body, std::move(type_params), std::move(type_param_bounds));
    }
}
