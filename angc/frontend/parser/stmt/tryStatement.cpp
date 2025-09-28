//
// Created by cv2 on 9/19/25.
//

#include "Parser.h"
namespace angara {


    std::shared_ptr<Stmt> Parser::tryStatement() {
            consume(TokenType::LEFT_BRACE, "Expect '{' after 'try'.");
            std::shared_ptr<Stmt> tryBlock = std::make_shared<BlockStmt>(block());

            consume(TokenType::CATCH, "Expect 'catch' after try block.");
            consume(TokenType::LEFT_PAREN, "Expect '(' after 'catch'.");
            Token catchName = consume(TokenType::IDENTIFIER, "Expect exception variable name.");

            // --- THIS IS THE FIX ---
            // Look for an optional type annotation.
            std::shared_ptr<ASTType> catchType = nullptr;
            if (match({TokenType::AS})) {
                    catchType = type(); // Reuse our powerful type parser
            }
            // --- END FIX ---

            consume(TokenType::RIGHT_PAREN, "Expect ')' after catch clause.");

            consume(TokenType::LEFT_BRACE, "Expect '{' after catch clause.");
            std::shared_ptr<Stmt> catchBlock = std::make_shared<BlockStmt>(block());

            return std::make_shared<TryStmt>(std::move(tryBlock), std::move(catchName), catchType, std::move(catchBlock));
    }

}