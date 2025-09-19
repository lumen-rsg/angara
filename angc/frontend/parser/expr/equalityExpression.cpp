//
// Created by cv2 on 9/19/25.
//

#include "Parser.h"
namespace angara {

    // equality → comparison ( ( "!=" | "==" ) comparison )*
    std::shared_ptr<Expr> Parser::equality() {
        // 1. Parse the left-hand side expression (a 'comparison' or higher precedence).
        std::shared_ptr<Expr> expr = comparison();

        // 2. Loop as long as we find an equality-level operator.
        while (match({TokenType::BANG_EQUAL, TokenType::EQUAL_EQUAL, TokenType::IS})) {
            Token op = previous();

            if (op.type == TokenType::IS) {
                // If the operator is 'is', the right-hand side is a TYPE, not an expression.
                std::shared_ptr<ASTType> type_rhs = type();
                expr = std::make_shared<IsExpr>(std::move(expr), std::move(op), std::move(type_rhs));
            } else {
                // Otherwise, it's a regular binary comparison, and the RHS is an expression.
                std::shared_ptr<Expr> right = comparison();
                expr = std::make_shared<Binary>(std::move(expr), std::move(op), std::move(right));
            }
        }
        return expr;
    }

}