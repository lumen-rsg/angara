#include "Parser.h"
namespace angara {


    std::shared_ptr<Stmt> Parser::tryStatement() {
            consume(TokenType::LEFT_BRACE, "Expected '{' after 'try'.");
            std::shared_ptr<Stmt> tryBlock = std::make_shared<BlockStmt>(block());

            consume(TokenType::CATCH, "Expected 'catch' after 'try' block.");
            consume(TokenType::LEFT_PAREN, "Expected '(' after 'catch'.");
            Token catchName = consume(TokenType::IDENTIFIER, "Expected exception variable name in 'catch' clause.");
            std::shared_ptr<ASTType> catchType = nullptr;
            if (match({TokenType::AS})) {
                    catchType = type();
            }

            consume(TokenType::RIGHT_PAREN, "Expected ')' after 'catch' variable declaration.");

            consume(TokenType::LEFT_BRACE, "Expected '{' to begin 'catch' block.");
            std::shared_ptr<Stmt> catchBlock = std::make_shared<BlockStmt>(block());

            return std::make_shared<TryStmt>(std::move(tryBlock), std::move(catchName), catchType, std::move(catchBlock));
    }

}
