#include "Parser.h"
namespace angara {

    std::shared_ptr<Stmt> Parser::varDeclaration(bool is_const) {
        Token name = consume(TokenType::IDENTIFIER, "Expect variable name.");

        // The 'as <type>' clause is now optional again.
        std::shared_ptr<ASTType> typeAnnotation = nullptr;
        if (match({TokenType::AS})) {
            typeAnnotation = type();
        }

        std::shared_ptr<Expr> initializer = nullptr;
        if (match({TokenType::EQUAL})) {
            initializer = expression();
        }

        // A variable MUST have either a type annotation or an initializer.
        if (!typeAnnotation && !initializer) {
            throw error(name, "A variable declaration must have an explicit type ('as <type>') or an initializer ('= <value>').");
        }

        if (is_const && !initializer) {
            throw error(name, "A 'const' variable must be initialized.");
        }

        consume(TokenType::SEMICOLON, "Expect ';' after variable declaration.");
        return std::make_shared<VarDeclStmt>(std::move(name), typeAnnotation, std::move(initializer), is_const);
    }

}