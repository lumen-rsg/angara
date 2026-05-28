#include "Parser.h"
namespace angara {

    std::shared_ptr<Stmt> Parser::whileStatement() {
        Token keyword = previous();

        consume(TokenType::LEFT_PAREN, "Expected '(' after 'while'.", "E211");
        std::shared_ptr<Expr> condition = expression();
        consume(TokenType::RIGHT_PAREN, "Expected ')' after 'while' condition.", "E212");

        consume(TokenType::LEFT_BRACE, "Expected '{' to begin 'while' loop body.", "E213");
        std::shared_ptr<Stmt> body = std::make_shared<BlockStmt>(block());

        return std::make_shared<WhileStmt>(std::move(keyword), std::move(condition), std::move(body));

    }

}
