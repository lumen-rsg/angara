//
// Created by cv2 on 9/19/25.
//
#include "Parser.h"
namespace angara {

    // unary → ( "!" | "-" ) unary | primary
    std::shared_ptr<Expr> Parser::unary() {
        if (match({TokenType::BANG, TokenType::MINUS, TokenType::PLUS_PLUS, TokenType::MINUS_MINUS})) {
            Token op = previous();
            std::shared_ptr<Expr> right = unary();
            // Check if it's an update operator
            if (op.type == TokenType::PLUS_PLUS || op.type == TokenType::MINUS_MINUS) {
                return std::make_shared<UpdateExpr>(std::move(right), std::move(op), true /* isPrefix */);
            }
            return std::make_shared<Unary>(std::move(op), std::move(right));
        }
        return call();
    }

}