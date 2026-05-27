//
// Created by cv2 on 9/19/25.
//
#include "Parser.h"
namespace angara {

    // unary → ( "!" | "-" | "*" ) unary | primary
    // *expr is pointer dereference (FFI only, enforced by type checker)
    std::shared_ptr<Expr> Parser::unary() {
        if (match({TokenType::BANG, TokenType::MINUS, TokenType::TILDE, TokenType::PLUS_PLUS, TokenType::MINUS_MINUS, TokenType::STAR})) {
            Token op = previous();
            std::shared_ptr<Expr> right = unary();
            if (op.type == TokenType::PLUS_PLUS || op.type == TokenType::MINUS_MINUS) {
                return std::make_shared<UpdateExpr>(std::move(right), std::move(op), true /* isPrefix */);
            }
            if (op.type == TokenType::STAR) {
                return std::make_shared<DerefExpr>(std::move(op), std::move(right));
            }
            return std::make_shared<Unary>(std::move(op), std::move(right));
        }
        return call();
    }

}