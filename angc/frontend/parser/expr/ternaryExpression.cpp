#include "Parser.h"

namespace angara {

    std::shared_ptr<Expr> Parser::ternary() {
        std::shared_ptr<Expr> expr = nil_coalescing();

        if (match({TokenType::QUESTION})) {
            std::shared_ptr<Expr> thenBranch = expression();
            consume(TokenType::COLON, "Expected ':' between ternary branches (condition ? then : else).", "E242");
            std::shared_ptr<Expr> elseBranch = ternary();
            expr = std::make_shared<TernaryExpr>(std::move(expr), std::move(thenBranch), std::move(elseBranch));
        }
        return expr;
    }

}
