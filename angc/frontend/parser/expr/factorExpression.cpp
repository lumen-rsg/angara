//
// Created by cv2 on 9/19/25.
//
#include "Parser.h"
namespace angara {

    // factor → unary ( ( "/" | "*" | "%" ) unary )*
    std::shared_ptr<Expr> Parser::factor() {
        std::shared_ptr<Expr> expr = unary();

        // Add TokenType::PERCENT to this list.
        while (match({TokenType::SLASH, TokenType::STAR, TokenType::PERCENT})) {
            Token op = previous();
            std::shared_ptr<Expr> right = unary();
            expr = std::make_shared<Binary>(std::move(expr), std::move(op), std::move(right));
        }
        return expr;
    }

}