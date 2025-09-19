//
// Created by cv2 on 9/19/25.
//
#include "Parser.h"
namespace angara {

    // letDecl → "let" IDENTIFIER as <type> ( "=" expression )? ";"
    std::shared_ptr<Stmt> Parser::varDeclaration(bool is_const) {
        Token name = consume(TokenType::IDENTIFIER, "Expect variable name.");

        std::shared_ptr<ASTType> typeAnnotation = nullptr;
        if (match({TokenType::AS})) {
            typeAnnotation = type();
        }

        std::shared_ptr<Expr> initializer = nullptr;
        if (match({TokenType::EQUAL})) {
            initializer = expression();
        }

        if (is_const && !initializer) {
            throw error(previous(), "A 'const' variable must be initialized.");
        }

        consume(TokenType::SEMICOLON, "Expect ';' after variable declaration.");

        // Pass the is_const flag to the updated constructor
        return std::make_shared<VarDeclStmt>(std::move(name), typeAnnotation, std::move(initializer), is_const);
    }

}