//
// Created by cv2 on 9/19/25.
//

#include "Parser.h"
namespace angara {

    std::shared_ptr<Stmt> Parser::ifStatement() {
        Token keyword = previous(); // The 'if' token
        consume(TokenType::LEFT_PAREN, "Expect '(' after 'if'.");

        std::shared_ptr<Expr> condition = nullptr;
        std::shared_ptr<VarDeclStmt> declaration = nullptr;

        if (match({TokenType::LET})) {
            // This is an `if let` binding, not a regular statement.
            // We must parse it manually and NOT expect a semicolon.
            Token name = consume(TokenType::IDENTIFIER, "Expect variable name after 'let' in 'if' condition.");

            // The type annotation is optional in `if let`.
            std::shared_ptr<ASTType> typeAnnotation = nullptr;
            if (match({TokenType::AS})) {
                typeAnnotation = type();
            }

            consume(TokenType::EQUAL, "Expect '=' to provide an initializer for 'if let'.");
            std::shared_ptr<Expr> initializer = expression();

            // Create the VarDeclStmt. It's implicitly a 'const' binding.
            declaration = std::make_shared<VarDeclStmt>(name, typeAnnotation, initializer, true);
        } else {
            // This is a regular `if` statement with a boolean condition.
            condition = expression();
        }

        consume(TokenType::RIGHT_PAREN, "Expect ')' after if condition.");

        // The rest of the logic for parsing the then/else branches is unchanged.
        consume(TokenType::LEFT_BRACE, "Expect '{' before if body.");
        std::shared_ptr<Stmt> thenBranch = std::make_shared<BlockStmt>(block());

        std::shared_ptr<Stmt> elseBranch = nullptr;
        if (match({TokenType::ORIF})) {
            elseBranch = ifStatement();
        } else if (match({TokenType::ELSE})) {
            consume(TokenType::LEFT_BRACE, "Expect '{' before else body.");
            elseBranch = std::make_shared<BlockStmt>(block());
        }

        return std::make_shared<IfStmt>(keyword, condition, thenBranch, elseBranch, declaration);
    }

}