#include "Parser.h"
namespace angara {

    std::shared_ptr<Expr> Parser::primary() {
        if (match({TokenType::FALSE, TokenType::TRUE, TokenType::NIL,
                   TokenType::NUMBER_INT, TokenType::NUMBER_FLOAT, TokenType::STRING,
                   TokenType::RAW_STRING, TokenType::BYTE_STRING,  // LANG-6
                   TokenType::CHAR})) {  // LANG-4
            return std::make_shared<Literal>(previous());
        }

        // LANG-3: interpolated string $"..."
        if (match({TokenType::INTERP_STRING})) {
            return parseInterpolatedString(previous());
        }

        if (match({TokenType::THIS})) {
            return std::make_shared<ThisExpr>(previous());
        }

        if (match({TokenType::SUPER})) {
            Token keyword = previous();
            if (match({TokenType::DOT})) {
                Token method = consume(TokenType::IDENTIFIER, "Expected method name after 'super.'.", "E230");
                return std::make_shared<SuperExpr>(keyword, method);
            }
            else if (check(TokenType::LEFT_PAREN)) {
                return std::make_shared<SuperExpr>(keyword, std::nullopt);
            }
            else {
                throw error(peek(), "Expected '.' (to call a super method) or '(' (to call the parent constructor) after 'super'.", "E231");
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
            consume(TokenType::RIGHT_BRACKET, "Expected ']' after list elements.", "E232");
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
                        throw error(peek(), "Expected a string or identifier as record key.", "E233");
                    }
                    keys.push_back(key);

                    consume(TokenType::COLON, "Expected ':' after record key.", "E234");
                    values.push_back(expression());
                } while (match({TokenType::COMMA}));
            }
            consume(TokenType::RIGHT_BRACE, "Expected '}' after record literal.", "E235");
            return std::make_shared<RecordExpr>(std::move(keys), std::move(values));
        }

        if (match({TokenType::LEFT_PAREN})) {
            Token paren = previous();

            // LANG-10: distinguish tuple (a, b, ...) from grouping (a).
            // Parse the first element; if a comma follows it's a tuple.
            if (check(TokenType::RIGHT_PAREN)) {
                // Empty parens: error — () is not a valid expression.
                throw error(peek(), "Empty parentheses '()' is not a valid expression.", "E236");
            }

            std::shared_ptr<Expr> first = expression();

            if (match({TokenType::COMMA})) {
                // Tuple: (expr, expr, ...) or (expr,) — at least 2 elements or trailing comma
                std::vector<std::shared_ptr<Expr>> elements;
                elements.push_back(std::move(first));

                if (!check(TokenType::RIGHT_PAREN)) {
                    do {
                        if (check(TokenType::RIGHT_PAREN)) break;
                        elements.push_back(expression());
                    } while (match({TokenType::COMMA}));
                }
                consume(TokenType::RIGHT_PAREN, "Expected ')' after tuple elements.", "E237");
                return std::make_shared<TupleExpr>(std::move(paren), std::move(elements));
            } else {
                // Grouping: single expression in parens
                consume(TokenType::RIGHT_PAREN, "Expected ')' after grouped expression.", "E238");
                return std::make_shared<Grouping>(std::move(first));
            }
        }

        if (match({TokenType::MATCH})) {
            return matchExpression();
        }

        if (match({TokenType::ASM})) {
            return asmExpression();
        }

        throw error(peek(), "Expected an expression (literal, variable, call, 'if', 'match', list, or record).", "E237");
    }

    std::shared_ptr<Expr> Parser::lambdaExpression(const Token& keyword) {
        consume(TokenType::LEFT_PAREN, "Expected '(' after 'func' in lambda expression.", "E238");

        std::vector<Token> param_names;
        std::vector<std::shared_ptr<ASTType>> param_types;
        std::vector<std::shared_ptr<Expr>> param_defaults;  // LANG-11

        if (!check(TokenType::RIGHT_PAREN)) {
            do {
                Token param_name = consume(TokenType::IDENTIFIER, "Expected parameter name in lambda.", "E239");
                std::shared_ptr<ASTType> param_type = nullptr;
                if (match({TokenType::AS})) {
                    param_type = type();
                }
                // LANG-11: parse default argument value
                std::shared_ptr<Expr> default_value = nullptr;
                if (match({TokenType::EQUAL})) {
                    default_value = expression();
                }
                param_names.push_back(std::move(param_name));
                param_types.push_back(std::move(param_type));
                param_defaults.push_back(std::move(default_value));
            } while (match({TokenType::COMMA}));
        }

        consume(TokenType::RIGHT_PAREN, "Expected ')' after lambda parameters.", "E240");

        std::shared_ptr<ASTType> returnType = nullptr;
        if (match({TokenType::MINUS_GREATER})) {
            returnType = type();
        }

        consume(TokenType::LEFT_BRACE, "Expected '{' for lambda body.", "E241");
        auto body = block();

        return std::make_shared<LambdaExpr>(keyword, std::move(param_names),
                                            std::move(param_types),
                                            std::move(param_defaults),
                                            returnType, std::move(body));
    }

}
