//
// Created by cv2 on 9/19/25.
//
#include "Parser.h"
namespace angara {

    // unary → ( "!" | "-" | "*" | "await" ) unary | primary
    // *expr is pointer dereference (FFI only, enforced by type checker)
    // await expr suspends until the future resolves (LIB-4)
    std::shared_ptr<Expr> Parser::unary() {
        if (match({TokenType::BANG, TokenType::MINUS, TokenType::TILDE, TokenType::PLUS_PLUS, TokenType::MINUS_MINUS, TokenType::STAR})) {
            Token op = previous();
            if (++m_recursionDepth > 256) {
                throw error(op, "Maximum recursion depth exceeded — too many nested unary operators.", "E248");
            }
            std::shared_ptr<Expr> right = unary();
            --m_recursionDepth;
            if (op.type == TokenType::PLUS_PLUS || op.type == TokenType::MINUS_MINUS) {
                return std::make_shared<UpdateExpr>(std::move(right), std::move(op), true /* isPrefix */);
            }
            if (op.type == TokenType::STAR) {
                return std::make_shared<DerefExpr>(std::move(op), std::move(right));
            }
            return std::make_shared<Unary>(std::move(op), std::move(right));
        }

        // LIB-4: await expression
        if (match({TokenType::AWAIT})) {
            Token keyword = previous();
            if (++m_recursionDepth > 256) {
                throw error(keyword, "Maximum recursion depth exceeded — too many nested await expressions.", "E248");
            }
            auto future_expr = unary();
            --m_recursionDepth;
            return std::make_shared<AwaitExpr>(std::move(keyword), std::move(future_expr));
        }

        return call();
    }

}