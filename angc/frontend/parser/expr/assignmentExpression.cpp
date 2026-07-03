#include "Parser.h"
namespace angara {

    std::shared_ptr<Expr> Parser::assignment() {
        std::shared_ptr<Expr> expr = range();  // LANG-1: range binds tighter than assignment

        if (match({TokenType::EQUAL, TokenType::PLUS_EQUAL, TokenType::MINUS_EQUAL,
                   TokenType::STAR_EQUAL, TokenType::SLASH_EQUAL})) {

            Token op = previous();
            std::shared_ptr<Expr> value = assignment();

            if (dynamic_cast<VarExpr *>(expr.get()) ||
                dynamic_cast<GetExpr *>(expr.get()) ||
                dynamic_cast<SubscriptExpr *>(expr.get())) {
                return std::make_shared<AssignExpr>(std::move(expr), op, std::move(value));
                }

            error(op, "Cannot assign to this expression. Only variables ('x'), field accesses ('obj.field'), and subscripts ('arr[i]') can be assigned to.", "E214");
                   }

        return expr;
    }

}
