//
// Created by cv2 on 9/19/25.
//

#include "Parser.h"
namespace angara {

    std::shared_ptr<Stmt> Parser::returnStatement() {
        Token keyword = previous();
        std::shared_ptr<Expr> value = nullptr;
        if (!check(TokenType::SEMICOLON)) {
            value = expression();
        }
        consume(TokenType::SEMICOLON, "Expect ';' after return value.");
        return std::make_shared<ReturnStmt>(std::move(keyword), std::move(value));
    }

}