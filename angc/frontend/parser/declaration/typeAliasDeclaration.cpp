#include "Parser.h"

namespace angara {

    std::shared_ptr<Stmt> Parser::typeAliasDeclaration() {
        Token name = consume(TokenType::IDENTIFIER, "Expected type alias name after 'type'.", "E414");
        consume(TokenType::EQUAL, "Expected '=' after type alias name.", "E415");
        auto aliased_type = type();
        consume(TokenType::SEMICOLON, "Expected ';' after type alias.", "E416");

        return std::make_shared<TypeAliasStmt>(std::move(name), std::move(aliased_type));
    }

}
