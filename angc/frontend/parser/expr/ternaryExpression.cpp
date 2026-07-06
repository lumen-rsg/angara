#include "Parser.h"

namespace angara {

    std::shared_ptr<Expr> Parser::ternary() {
        std::shared_ptr<Expr> expr = nil_coalescing();

        if (match({TokenType::QUESTION})) {
            Token questionToken = previous();
            std::shared_ptr<Expr> thenBranch = expression();
            consume(TokenType::COLON, "Expected ':' between ternary branches (condition ? then : else).", "E242");
            if (++m_recursionDepth > 256) {
                throw error(previous(), "Maximum recursion depth exceeded — too many chained ternary expressions.", "E248");
            }
            std::shared_ptr<Expr> elseBranch = ternary();
            --m_recursionDepth;
            expr = std::make_shared<TernaryExpr>(std::move(expr), std::move(thenBranch), std::move(elseBranch),
                                                 std::move(questionToken));
        }
        return expr;
    }

}
