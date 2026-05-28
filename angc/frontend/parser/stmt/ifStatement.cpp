#include "Parser.h"
namespace angara {

    std::shared_ptr<Stmt> Parser::ifStatement() {
        Token keyword = previous();
        consume(TokenType::LEFT_PAREN, "Expected '(' after 'if'.", "E195");

        std::shared_ptr<Expr> condition = nullptr;
        std::shared_ptr<VarDeclStmt> declaration = nullptr;

        if (match({TokenType::LET})) {
            Token name = consume(TokenType::IDENTIFIER, "Expected variable name after 'let' in 'if' condition.", "E196");

            std::shared_ptr<ASTType> typeAnnotation = nullptr;
            if (match({TokenType::AS})) {
                typeAnnotation = type();
            }

            consume(TokenType::EQUAL, "Expected '=' with an initializer for 'if let' variable.", "E197");
            std::shared_ptr<Expr> initializer = expression();

            declaration = std::make_shared<VarDeclStmt>(name, typeAnnotation, initializer, true);
        } else {
            condition = expression();
        }

        consume(TokenType::RIGHT_PAREN, "Expected ')' after 'if' condition.", "E198");
        std::shared_ptr<Stmt> thenBranch = statement();
        std::shared_ptr<Stmt> elseBranch = nullptr;
        if (match({TokenType::ORIF})) {
            elseBranch = ifStatement();
        } else if (match({TokenType::ELSE})) {
            elseBranch = statement();
        }

        return std::make_shared<IfStmt>(keyword, condition, thenBranch, elseBranch, declaration);
    }

}
