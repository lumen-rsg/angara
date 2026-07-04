#include "Parser.h"
namespace angara {

    std::shared_ptr<Stmt> Parser::varDeclaration(bool is_const) {
        // LANG-10: destructuring declaration — `let (a, b) = expr;`
        if (match({TokenType::LEFT_PAREN})) {
            std::vector<Token> names;
            if (!check(TokenType::RIGHT_PAREN)) {
                do {
                    Token var_name = consume(TokenType::IDENTIFIER, "Expected variable name in destructuring pattern.", "E207");
                    names.push_back(var_name);
                } while (match({TokenType::COMMA}));
            }
            consume(TokenType::RIGHT_PAREN, "Expected ')' after destructuring pattern.", "E211");

            if (names.empty()) {
                throw error(previous(), "Destructuring pattern must contain at least one variable name.", "E212");
            }

            std::shared_ptr<ASTType> typeAnnotation = nullptr;
            if (match({TokenType::AS})) {
                typeAnnotation = type();
            }

            std::shared_ptr<Expr> initializer = nullptr;
            if (match({TokenType::EQUAL})) {
                initializer = expression();
            }

            if (!typeAnnotation && !initializer) {
                throw error(names[0], "Destructuring declaration requires either a type annotation ('as <type>') or an initializer ('= <value>').", "E208");
            }

            if (is_const && !initializer) {
                throw error(names[0], "A 'const' destructuring requires an initializer ('= <value>').", "E209");
            }

            consume(TokenType::SEMICOLON, "Expected ';' after destructuring declaration.", "E210");
            return std::make_shared<VarDeclStmt>(std::move(names), typeAnnotation, std::move(initializer), is_const);
        }

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
