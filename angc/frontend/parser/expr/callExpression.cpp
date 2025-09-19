//
// Created by cv2 on 9/19/25.
//
#include "Parser.h"
namespace angara {

    std::shared_ptr<Expr> Parser::call() {
        // 1. Parse the primary expression (e.g., the variable, literal, or grouped expr).
        std::shared_ptr<Expr> expr = primary();

        // 2. Loop to handle chained calls like `a.b()?.c`.
        while (true) {
            if (match({TokenType::LEFT_PAREN})) {
                // --- Handle Function Call: `(...)` ---
                std::vector<std::shared_ptr<Expr>> arguments;
                if (!check(TokenType::RIGHT_PAREN)) {
                    do {
                        if (arguments.size() >= 255) {
                            error(peek(), "Cannot have more than 255 arguments.");
                        }
                        arguments.push_back(expression());
                    } while (match({TokenType::COMMA}));
                }
                Token paren = consume(TokenType::RIGHT_PAREN, "Expect ')' after arguments.");
                expr = std::make_shared<CallExpr>(std::move(expr), std::move(paren), std::move(arguments));

            } else if (match({TokenType::LEFT_BRACKET})) {
                // --- Handle Subscript: `[...]` ---
                Token bracket = previous();
                std::shared_ptr<Expr> index = expression();
                consume(TokenType::RIGHT_BRACKET, "Expect ']' after subscript index.");
                expr = std::make_shared<SubscriptExpr>(std::move(expr), std::move(bracket), std::move(index));

            } else if (match({TokenType::PLUS_PLUS, TokenType::MINUS_MINUS})) {
                // --- Handle Postfix Update: `++` or `--` ---
                Token op = previous();
                expr = std::make_shared<UpdateExpr>(std::move(expr), std::move(op), false /* isPrefix */);

            } else if (match({TokenType::DOT, TokenType::QUESTION_DOT})) {
                // ---  Handle Property Access: `.` or `?.` ---
                Token op = previous(); // This is the '.' or '?.' token.
                Token name = consume(TokenType::IDENTIFIER, "Expect property name after '.' or '?.'.");

                // Pass all three parts to the updated GetExpr constructor.
                expr = std::make_shared<GetExpr>(std::move(expr), op, std::move(name));

            } else {
                // No more chained operators found, so exit the loop.
                break;
            }
        }

        return expr;
    }

}