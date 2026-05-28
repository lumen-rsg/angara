#include "Parser.h"
namespace angara {

    std::shared_ptr<Stmt> Parser::breakStatement() {
        Token keyword = previous();
        consume(TokenType::SEMICOLON, "Expected ';' after 'break'.", "E177");
        return std::make_shared<BreakStmt>(std::move(keyword));
    }

    std::shared_ptr<Stmt> Parser::continueStatement() {
        Token keyword = previous();
        consume(TokenType::SEMICOLON, "Expected ';' after 'continue'.", "E178");
        return std::make_shared<ContinueStmt>(std::move(keyword));
    }

}
