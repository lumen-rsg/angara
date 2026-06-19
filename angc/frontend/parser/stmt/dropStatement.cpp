#include "Parser.h"
namespace angara {
    std::shared_ptr<Stmt> Parser::dropStatement() {
        Token name = consume(TokenType::IDENTIFIER,
            "Expected a variable name after 'drop'.", "E501");
        consume(TokenType::SEMICOLON,
            "Expected ';' after 'drop' statement.", "E502");
        return std::make_shared<DropStmt>(std::move(name));
    }
}
