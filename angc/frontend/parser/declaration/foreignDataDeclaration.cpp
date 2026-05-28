#include "Parser.h"
namespace angara {

    std::shared_ptr<Stmt> Parser::foreignDataDeclaration() {
        Token name = consume(TokenType::IDENTIFIER, "Expected name after 'foreign data'.", "E164");

        // Opaque type: "foreign data FILE;"
        if (match({TokenType::SEMICOLON})) {
            auto stmt = std::make_shared<DataStmt>(std::move(name), std::vector<std::shared_ptr<VarDeclStmt>>{});
            stmt->is_foreign = true;
            stmt->is_opaque = true;
            return stmt;
        }

        // Structured type: "foreign data utsname { sysname as i8[256]; ... }"
        consume(TokenType::LEFT_BRACE, "Expected '{' or ';' after foreign data name.", "E165");

        std::vector<std::shared_ptr<VarDeclStmt>> fields;
        while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
            Token field_name = consume(TokenType::IDENTIFIER, "Expected field name in foreign data block.", "E166");
            consume(TokenType::AS, "Expected 'as' followed by a type for foreign data field.", "E167");
            std::shared_ptr<ASTType> type_ann = type();
            consume(TokenType::SEMICOLON, "Expected ';' after foreign data field declaration.", "E168");

            fields.push_back(std::make_shared<VarDeclStmt>(field_name, type_ann, nullptr, false));
        }

        consume(TokenType::RIGHT_BRACE, "Expected '}' after foreign data body.", "E169");
        auto stmt = std::make_shared<DataStmt>(std::move(name), std::move(fields));
        stmt->is_foreign = true;
        return stmt;
    }

}
