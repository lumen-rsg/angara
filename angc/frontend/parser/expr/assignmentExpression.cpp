//
// Created by cv2 on 9/19/25.
//
#include "Parser.h"
namespace angara {

    std::shared_ptr<Expr> Parser::assignment() {
        // 1. Parse the left-hand side, which could be a variable, property, etc.
        std::shared_ptr<Expr> expr = ternary();

        // 2. Check if the next token is ANY assignment operator.
        if (match({TokenType::EQUAL, TokenType::PLUS_EQUAL, TokenType::MINUS_EQUAL,
                   TokenType::STAR_EQUAL, TokenType::SLASH_EQUAL})) {

            Token op = previous();
            std::shared_ptr<Expr> value = assignment(); // Recursively parse the right-hand side.

            // 3. Check if the left-hand side is a valid target.
            if (dynamic_cast<VarExpr *>(expr.get()) ||
                dynamic_cast<GetExpr *>(expr.get()) ||
                dynamic_cast<SubscriptExpr *>(expr.get())) {
                // 4. If it's a valid target, create the AssignExpr, passing the operator token.
                return std::make_shared<AssignExpr>(std::move(expr), op, std::move(value));
                }

            error(op, "Invalid assignment target.");
                   }

        // 5. If no assignment operator was found, return the parsed expression.
        return expr;
    }

}