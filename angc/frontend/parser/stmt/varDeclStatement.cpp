#include "Parser.h"
namespace angara {

// letDecl → ("let" | "const") IDENTIFIER "as" <type> ( "=" expression )? ";"
    std::shared_ptr<Stmt> Parser::varDeclaration(bool is_const) {
        Token name = consume(TokenType::IDENTIFIER, "Expect variable name.");

        // The 'as <type>' clause is no longer optional.
        consume(TokenType::AS, "Expect 'as <type>' to follow a variable name. Angara requires explicit type annotations for declarations.");
        std::shared_ptr<ASTType> typeAnnotation = type();

        std::shared_ptr<Expr> initializer = nullptr;
        if (match({TokenType::EQUAL})) {
            initializer = expression();
        }

        // A 'const' must still be initialized. This rule is independent of type annotation.
        if (is_const && !initializer) {
            throw error(previous(), "A 'const' variable must be initialized.");
        }

        consume(TokenType::SEMICOLON, "Expect ';' after variable declaration.");
        return std::make_shared<VarDeclStmt>(std::move(name), typeAnnotation, std::move(initializer), is_const);
    }

}