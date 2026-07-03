#include "Parser.h"
namespace angara {

    // LANG-1: range expression — `start..end` (exclusive) or `start...end` (inclusive).
    // Binds looser than ternary (so `a ? b : c..d` parses the range after the ternary),
    // tighter than assignment.
    std::shared_ptr<Expr> Parser::range() {
        std::shared_ptr<Expr> expr = ternary();

        if (match({TokenType::DOT_DOT, TokenType::DOT_DOT_DOT})) {
            Token op = previous();
            std::shared_ptr<Expr> right = ternary();
            return std::make_shared<RangeExpr>(std::move(expr), std::move(op), std::move(right));
        }
        return expr;
    }

}
