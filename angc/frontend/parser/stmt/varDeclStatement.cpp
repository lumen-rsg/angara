#include "Parser.h"
namespace angara {

    std::shared_ptr<Stmt> Parser::varDeclaration(bool is_const) {
        Token name = consume(TokenType::IDENTIFIER, "Expected variable name.", "E207");
        std::shared_ptr<ASTType> typeAnnotation = nullptr;
        if (match({TokenType::AS})) {
            typeAnnotation = type();
        }

        std::shared_ptr<Expr> initializer = nullptr;
        if (match({TokenType::EQUAL})) {
            initializer = expression();
        }

        if (!typeAnnotation && !initializer) {
            throw error(name, "Variable declaration requires either a type annotation ('as <type>') or an initializer ('= <value>').", "E208");
        }

        if (is_const && !initializer) {
            throw error(name, "A 'const' variable requires an initializer ('= <value>').", "E209");
        }

        consume(TokenType::SEMICOLON, "Expected ';' after variable declaration.", "E210");
        return std::make_shared<VarDeclStmt>(std::move(name), typeAnnotation, std::move(initializer), is_const);
    }

}
