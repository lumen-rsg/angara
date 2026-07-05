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

                    // LANG-10: destructured parameter — (a, b) as (T1, T2)
                    std::vector<Token> destructure_names;
                    Token param_name;
                    if (match({TokenType::LEFT_PAREN})) {
                        do {
                            if (check(TokenType::RIGHT_PAREN)) break;
                            destructure_names.push_back(
                                consume(TokenType::IDENTIFIER, "Expected variable name in destructured parameter.", "E190"));
                        } while (match({TokenType::COMMA}));
                        consume(TokenType::RIGHT_PAREN, "Expected ')' after destructured parameter names.", "E413");
                        if (destructure_names.empty()) {
                            throw error(previous(), "Destructured parameter must contain at least one variable name.", "E414");
                        }
                        // Use the first name as the primary parameter name for symbol purposes
                        param_name = destructure_names[0];
                    } else {
                        param_name = consume(TokenType::IDENTIFIER, "Expected parameter name.", "E190");
                    }

                    consume(TokenType::AS, "Expected 'as' followed by a type after parameter name.", "E191");
                    std::shared_ptr<ASTType> param_type = type();
                    bool is_variadic = match({TokenType::DOT_DOT_DOT});

                    // LANG-11: parse default argument value
                    std::shared_ptr<Expr> default_value = nullptr;
                    if (match({TokenType::EQUAL})) {
                        default_value = expression();
                    }

                    parameters.push_back({param_name, param_type, is_variadic, default_value, std::move(destructure_names)});

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
