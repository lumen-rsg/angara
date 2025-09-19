//
// Created by cv2 on 9/19/25.
//
#include "Parser.h"
namespace angara {

    // comparison → term ( ( ">" | ">=" | "<" | "<=" ) term )*
    std::shared_ptr<Expr> Parser::comparison() {
        std::shared_ptr<Expr> expr = term();
        while (match({TokenType::GREATER, TokenType::GREATER_EQUAL, TokenType::LESS, TokenType::LESS_EQUAL})) {
            Token op = previous();
            std::shared_ptr<Expr> right = term();
            expr = std::make_shared<Binary>(std::move(expr), std::move(op), std::move(right));
        }
        return expr;
    }

}