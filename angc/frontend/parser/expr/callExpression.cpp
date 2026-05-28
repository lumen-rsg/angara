#include "Parser.h"

namespace angara {

    std::shared_ptr<Expr> Parser::call() {
        std::shared_ptr<Expr> expr = primary();

        while (true) {
            if (match({TokenType::LEFT_PAREN})) {
                std::vector<std::shared_ptr<Expr>> arguments;
                if (!check(TokenType::RIGHT_PAREN)) {
                    do {
                        if (arguments.size() >= 255) {
                            error(peek(), "Too many arguments in function call — maximum is 255.", "E215");
                        }
                        arguments.push_back(expression());
                    } while (match({TokenType::COMMA}));
                }
                Token paren = consume(TokenType::RIGHT_PAREN, "Expected ')' after function arguments.", "E216");
                expr = std::make_shared<CallExpr>(std::move(expr), std::move(paren), std::move(arguments));

            } else if (match({TokenType::LEFT_BRACKET})) {
                Token bracket = previous();
                std::shared_ptr<Expr> index = expression();
                consume(TokenType::RIGHT_BRACKET, "Expected ']' after subscript index.", "E217");
                expr = std::make_shared<SubscriptExpr>(std::move(expr), std::move(bracket), std::move(index));

            } else if (match({TokenType::PLUS_PLUS, TokenType::MINUS_MINUS})) {
                Token op = previous();
                expr = std::make_shared<UpdateExpr>(std::move(expr), std::move(op), false /* isPrefix */);

            } else if (match({TokenType::DOT, TokenType::QUESTION_DOT})) {
                Token op = previous();
                Token name = consume(TokenType::IDENTIFIER, "Expected property or method name after '.'.", "E218");
                expr = std::make_shared<GetExpr>(std::move(expr), op, std::move(name));

            } else {
                break;
            }
        }

        return expr;
    }

}
