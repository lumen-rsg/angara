//
// Created by cv2 on 9/19/25.
//
#include "Parser.h"
namespace angara {

    std::shared_ptr<Stmt> Parser::breakStatement() {
        Token keyword = previous(); // The 'break' token we just matched
        consume(TokenType::SEMICOLON, "Expect ';' after 'break'.");
        return std::make_shared<BreakStmt>(std::move(keyword));
    }

    std::shared_ptr<Stmt> Parser::continueStatement() {
        Token keyword = previous(); // The 'continue' token we just matched
        consume(TokenType::SEMICOLON, "Expect ';' after 'continue'.");
        return std::make_shared<ContinueStmt>(std::move(keyword));
    }

}
