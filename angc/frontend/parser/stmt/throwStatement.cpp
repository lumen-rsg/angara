//
// Created by cv2 on 9/19/25.
//
#include "Parser.h"
namespace angara {

    std::shared_ptr<Stmt> Parser::throwStatement() {
        Token keyword = previous();
        std::shared_ptr<Expr> expr = expression();
        consume(TokenType::SEMICOLON, "Expect ';' after throw value.");
        return std::make_shared<ThrowStmt>(std::move(keyword), std::move(expr));
    }

}