//
// Created by cv2 on 9/19/25.
//

#include "Parser.h"
namespace angara {

    std::shared_ptr<Stmt> Parser::whileStatement() {
        Token keyword = previous();

        consume(TokenType::LEFT_PAREN, "Expect '(' after 'while'.");
        std::shared_ptr<Expr> condition = expression();
        consume(TokenType::RIGHT_PAREN, "Expect ')' after while condition.");

        consume(TokenType::LEFT_BRACE, "Expect '{' to begin while loop body.");
        std::shared_ptr<Stmt> body = std::make_shared<BlockStmt>(block());

        return std::make_shared<WhileStmt>(std::move(keyword), std::move(condition), std::move(body));

    }

}