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

        // If none of the above matched, it's an error.
        throw error(peek(), "Expect expression.");
    }

}