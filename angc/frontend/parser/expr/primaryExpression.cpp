//
// Created by cv2 on 9/19/25.
//
#include "Parser.h"
namespace angara {

// primary → NUMBER | STRING | "true" | "false" | IDENTIFIER | "(" expression ")"
    std::shared_ptr<Expr> Parser::primary() {
        if (match({TokenType::FALSE, TokenType::TRUE, TokenType::NIL,
                   TokenType::NUMBER_INT, TokenType::NUMBER_FLOAT, TokenType::STRING})) {
            return std::make_shared<Literal>(previous());
        }

        if (match({TokenType::RETYPE})) {
            Token keyword = previous();
            consume(TokenType::LESS, "Expect '<' after 'retype'.");
            auto target_type = type();
            consume(TokenType::GREATER, "Expect '>' after retype target type.");
            consume(TokenType::LEFT_PAREN, "Expect '(' after 'retype<T>'.");
            auto expr = expression(); // The c_ptr to be retyped
            consume(TokenType::RIGHT_PAREN, "Expect ')' to close 'retype<T>(...)'.");
            return std::make_shared<RetypeExpr>(keyword, target_type, expr);
        }

        if (match({TokenType::SIZEOF})) { // Assuming you add SIZEOF to the lexer
            Token keyword = previous();
            consume(TokenType::LESS, "Expect '<' after 'sizeof'.");
            auto type_arg = type(); // Reuse our existing type parser
            consume(TokenType::GREATER, "Expect '>' after type argument.");
            consume(TokenType::LEFT_PAREN, "Expect '()' after 'sizeof<T>'.");
            consume(TokenType::RIGHT_PAREN, "Expect ')' to close 'sizeof<T>()'.");
            return std::make_shared<SizeofExpr>(keyword, type_arg);
        }


        if (match({TokenType::THIS})) {
            return std::make_shared<ThisExpr>(previous());
        }

        if (match({TokenType::SUPER})) {
            Token keyword = previous(); // The 'super' token

            // --- Look ahead for '.' or '(' ---
            if (match({TokenType::DOT})) {
                // It's a super.method() call.
                Token method = consume(TokenType::IDENTIFIER, "Expect superclass method name after 'super.'.");
                return std::make_shared<SuperExpr>(keyword, method);
            }
            else if (check(TokenType::LEFT_PAREN)) {
                // It's a super(...) constructor call. The method is implicitly 'init'.
                // We don't consume the parenthesis here; the `call()` function will do that.
                return std::make_shared<SuperExpr>(keyword, std::nullopt);
            }
            else {
                throw error(peek(), "Expect '.' or '(' after 'super'.");
            }
        }

        // Lambda expression: func(params) -> type { body }
        if (match({TokenType::FUNC})) {
            Token keyword = previous();
            return lambdaExpression(keyword);
        }

        if (match({TokenType::IDENTIFIER})) {
            return std::make_shared<VarExpr>(previous());
        }

        // List literals
        if (match({TokenType::LEFT_BRACKET})) {
            Token bracket = previous();
            std::vector<std::shared_ptr<Expr>> elements;
            if (!check(TokenType::RIGHT_BRACKET)) {
                do {
                    if (check(TokenType::RIGHT_BRACKET)) break;
                    elements.push_back(expression());
                } while (match({TokenType::COMMA}));
            }
            consume(TokenType::RIGHT_BRACKET, "Expect ']' after list elements.");
            return std::make_shared<ListExpr>(std::move(bracket), std::move(elements));
        }

        // Record literals
        if (match({TokenType::LEFT_BRACE})) {
            std::vector<Token> keys;
            std::vector<std::shared_ptr<Expr>> values;
            if (!check(TokenType::RIGHT_BRACE)) {
                do {
                    if (check(TokenType::RIGHT_BRACE)) break; // Allow trailing comma

                    Token key;
                    if (match({TokenType::STRING})) {
                        key = previous();
                    } else if (match({TokenType::IDENTIFIER})) {
                        key = previous();
                        key.type = TokenType::STRING;
                    } else {
                        throw error(peek(), "Expect string or identifier for record key.");
                    }
                    keys.push_back(key);

                    consume(TokenType::COLON, "Expect ':' after key in record literal.");
                    values.push_back(expression());
                } while (match({TokenType::COMMA}));
            }
            consume(TokenType::RIGHT_BRACE, "Expect '}' after record fields.");
            return std::make_shared<RecordExpr>(std::move(keys), std::move(values));
        }

        // Grouping
        if (match({TokenType::LEFT_PAREN})) {
            std::shared_ptr<Expr> expr = expression();
            consume(TokenType::RIGHT_PAREN, "Expect ')' after expression.");
            return std::make_shared<Grouping>(std::move(expr));
        }

        if (match({TokenType::MATCH})) {
            return matchExpression();
        }

        // If none of the above matched, it's an error.
        throw error(peek(), "Expect expression.");
    }

    // Parses: (name as type, ...) -> ret_type { body }
    // The 'func' keyword has already been consumed; `keyword` is that token.
    std::shared_ptr<Expr> Parser::lambdaExpression(const Token& keyword) {
        consume(TokenType::LEFT_PAREN, "Expect '(' after 'func' in lambda expression.");

        std::vector<Token> param_names;
        std::vector<std::shared_ptr<ASTType>> param_types;

        if (!check(TokenType::RIGHT_PAREN)) {
            do {
                Token param_name = consume(TokenType::IDENTIFIER, "Expect parameter name.");
                std::shared_ptr<ASTType> param_type = nullptr;
                if (match({TokenType::AS})) {
                    param_type = type();
                }
                param_names.push_back(std::move(param_name));
                param_types.push_back(std::move(param_type));
            } while (match({TokenType::COMMA}));
        }

        consume(TokenType::RIGHT_PAREN, "Expect ')' after lambda parameters.");

        // Optional return type annotation: -> type
        std::shared_ptr<ASTType> returnType = nullptr;
        if (match({TokenType::MINUS_GREATER})) {
            returnType = type();
        }

        // Body is mandatory for lambdas
        consume(TokenType::LEFT_BRACE, "Expect '{' for lambda body.");
        auto body = block();

        return std::make_shared<LambdaExpr>(keyword, std::move(param_names),
                                            std::move(param_types), returnType,
                                            std::move(body));
    }

}
