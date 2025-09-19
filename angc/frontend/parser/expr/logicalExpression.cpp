//
// Created by cv2 on 9/19/25.
//
#include "Parser.h"
namespace angara {

    // Logical OR (left-associative)
    std::shared_ptr<Expr> Parser::logic_or() {
        std::shared_ptr<Expr> expr = logic_and();
        while (match({TokenType::LOGICAL_OR})) {
            Token op = previous();
            std::shared_ptr<Expr> right = logic_and();
            expr = std::make_shared<LogicalExpr>(std::move(expr), std::move(op), std::move(right));
        }
        return expr;
    }

    // Logical AND (left-associative)
    std::shared_ptr<Expr> Parser::logic_and() {
        std::shared_ptr<Expr> expr = equality();
        while (match({TokenType::LOGICAL_AND})) {
            Token op = previous();
            std::shared_ptr<Expr> right = equality();
            expr = std::make_shared<LogicalExpr>(std::move(expr), std::move(op), std::move(right));
        }
        return expr;
    }

    std::shared_ptr<Expr> Parser::nil_coalescing() {
        std::shared_ptr<Expr> expr = logic_or(); // Its left-hand side is higher precedence

        while (match({TokenType::QUESTION_QUESTION})) {
            Token op = previous();
            // The right-hand side is also higher precedence.
            std::shared_ptr<Expr> right = logic_or();
            // We will reuse the LogicalExpr AST node for `??`. The TypeChecker will
            // give it its special meaning.
            expr = std::make_shared<LogicalExpr>(std::move(expr), std::move(op), std::move(right));
        }
        return expr;
    }

}