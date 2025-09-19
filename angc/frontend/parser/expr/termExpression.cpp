//
// Created by cv2 on 9/19/25.
//
#include "Parser.h"
namespace angara {

    // term → factor ( ( "-" | "+" ) factor )*
    std::shared_ptr<Expr> Parser::term() {
        std::shared_ptr<Expr> expr = factor();
        while (match({TokenType::MINUS, TokenType::PLUS})) {
            Token op = previous();
            std::shared_ptr<Expr> right = factor();
            expr = std::make_shared<Binary>(std::move(expr), std::move(op), std::move(right));
        }
        return expr;
    }

}