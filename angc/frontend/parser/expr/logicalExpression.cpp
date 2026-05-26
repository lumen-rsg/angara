//
// Created by cv2 on 9/19/25.
//
#include "Parser.h"
namespace angara {

    std::shared_ptr<Expr> Parser::logic_or() {
        std::shared_ptr<Expr> expr = logic_and();
        while (match({TokenType::LOGICAL_OR})) {
            Token op = previous();
            std::shared_ptr<Expr> right = logic_and();
            expr = std::make_shared<LogicalExpr>(std::move(expr), std::move(op), std::move(right));
        }
        return expr;
    }

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
        std::shared_ptr<Expr> expr = logic_or();

        while (match({TokenType::QUESTION_QUESTION})) {
            Token op = previous();
            std::shared_ptr<Expr> right = logic_or();
            expr = std::make_shared<LogicalExpr>(std::move(expr), std::move(op), std::move(right));
        }
        return expr;
    }

}