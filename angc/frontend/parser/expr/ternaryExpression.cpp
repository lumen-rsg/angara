//
// Created by cv2 on 9/19/25.
//

#include "Parser.h"

namespace angara {

    std::shared_ptr<Expr> Parser::ternary() {
        // The condition can now be a nil-coalescing expression.
        std::shared_ptr<Expr> expr = nil_coalescing();

        if (match({TokenType::QUESTION})) {
            std::shared_ptr<Expr> thenBranch = expression();
            consume(TokenType::COLON, "Expect ':' for ternary operator.");
            std::shared_ptr<Expr> elseBranch = ternary(); // Right-associative
            expr = std::make_shared<TernaryExpr>(std::move(expr), std::move(thenBranch), std::move(elseBranch));
        }
        return expr;
    }

}