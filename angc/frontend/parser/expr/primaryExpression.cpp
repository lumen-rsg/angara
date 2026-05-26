#include "Parser.h"
namespace angara {

    std::shared_ptr<Expr> Parser::primary() {
        if (match({TokenType::FALSE, TokenType::TRUE, TokenType::NIL,
                   TokenType::NUMBER_INT, TokenType::NUMBER_FLOAT, TokenType::STRING})) {
            return std::make_shared<Literal>(previous());
        }

        if (match({TokenType::RETYPE})) {
            Token keyword = previous();
            consume(TokenType::LESS, "Expected '<' after 'retype'.");
            auto target_type = type();
            consume(TokenType::GREATER, "Expected '>' after target type in 'retype<T>'.");
            consume(TokenType::LEFT_PAREN, "Expected '(' after 'retype<T>'.");
            auto expr = expression();
            consume(TokenType::RIGHT_PAREN, "Expected ')' to close 'retype<T>(expr)'.");
            return std::make_shared<RetypeExpr>(keyword, target_type, expr);
        }

        if (match({TokenType::SIZEOF})) {
            Token keyword = previous();
            consume(TokenType::LESS, "Expected '<' after 'sizeof'.");
            auto type_arg = type();
            consume(TokenType::GREATER, "Expected '>' after type argument in 'sizeof<T>'.");
            consume(TokenType::LEFT_PAREN, "Expected '()' after 'sizeof<T>'.");
            consume(TokenType::RIGHT_PAREN, "Expected ')' to close 'sizeof<T>()'.");
            return std::make_shared<SizeofExpr>(keyword, type_arg);
        }


        if (match({TokenType::THIS})) {
            return std::make_shared<ThisExpr>(previous());
        }

        if (match({TokenType::SUPER})) {
            Token keyword = previous();
            if (match({TokenType::DOT})) {
                Token method = consume(TokenType::IDENTIFIER, "Expected method name after 'super.'.");
                return std::make_shared<SuperExpr>(keyword, method);
            }
            else if (check(TokenType::LEFT_PAREN)) {
                return std::make_shared<SuperExpr>(keyword, std::nullopt);
            }
            else {
                throw error(peek(), "Expected '.' (to call a super method) or '(' (to call the parent constructor) after 'super'.");
            }
        }

        if (match({TokenType::FUNC})) {
            Token keyword = previous();
            return lambdaExpression(keyword);
        }

        if (match({TokenType::IDENTIFIER})) {
            return std::make_shared<VarExpr>(previous());
        }

        if (match({TokenType::LEFT_BRACKET})) {
            Token bracket = previous();
            std::vector<std::shared_ptr<Expr>> elements;
            if (!check(TokenType::RIGHT_BRACKET)) {
                do {
                    if (check(TokenType::RIGHT_BRACKET)) break;
                    elements.push_back(expression());
                } while (match({TokenType::COMMA}));
            }
            consume(TokenType::RIGHT_BRACKET, "Expected ']' after list elements.");
            return std::make_shared<ListExpr>(std::move(bracket), std::move(elements));
        }

        if (match({TokenType::LEFT_BRACE})) {
            std::vector<Token> keys;
            std::vector<std::shared_ptr<Expr>> values;
            if (!check(TokenType::RIGHT_BRACE)) {
                do {
                    if (check(TokenType::RIGHT_BRACE)) break;

                    Token key;
                    if (match({TokenType::STRING})) {
                        key = previous();
                    } else if (match({TokenType::IDENTIFIER})) {
                        key = previous();
                        key.type = TokenType::STRING;
                    } else {
                        throw error(peek(), "Expected a string or identifier as record key.");
                    }
                    keys.push_back(key);

                    consume(TokenType::COLON, "Expected ':' after record key.");
                    values.push_back(expression());
                } while (match({TokenType::COMMA}));
            }
            consume(TokenType::RIGHT_BRACE, "Expected '}' after record literal.");
            return std::make_shared<RecordExpr>(std::move(keys), std::move(values));
        }

        if (match({TokenType::LEFT_PAREN})) {
            std::shared_ptr<Expr> expr = expression();
            consume(TokenType::RIGHT_PAREN, "Expected ')' after grouped expression.");
            return std::make_shared<Grouping>(std::move(expr));
        }

        if (match({TokenType::MATCH})) {
            return matchExpression();
        }

        throw error(peek(), "Expected an expression (literal, variable, call, 'if', 'match', list, or record).");
    }

    std::shared_ptr<Expr> Parser::lambdaExpression(const Token& keyword) {
        consume(TokenType::LEFT_PAREN, "Expected '(' after 'func' in lambda expression.");

        std::vector<Token> param_names;
        std::vector<std::shared_ptr<ASTType>> param_types;

        if (!check(TokenType::RIGHT_PAREN)) {
            do {
                Token param_name = consume(TokenType::IDENTIFIER, "Expected parameter name in lambda.");
                std::shared_ptr<ASTType> param_type = nullptr;
                if (match({TokenType::AS})) {
                    param_type = type();
                }
                param_names.push_back(std::move(param_name));
                param_types.push_back(std::move(param_type));
            } while (match({TokenType::COMMA}));
        }

        consume(TokenType::RIGHT_PAREN, "Expected ')' after lambda parameters.");

        std::shared_ptr<ASTType> returnType = nullptr;
        if (match({TokenType::MINUS_GREATER})) {
            returnType = type();
        }

        consume(TokenType::LEFT_BRACE, "Expected '{' for lambda body.");
        auto body = block();

        return std::make_shared<LambdaExpr>(keyword, std::move(param_names),
                                            std::move(param_types), returnType,
                                            std::move(body));
    }

}
