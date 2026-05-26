#include "Parser.h"
namespace angara {

    std::shared_ptr<Stmt> Parser::dataDeclaration() {
        Token name = consume(TokenType::IDENTIFIER, "Expected name after 'data'.");
        auto type_params = parseTypeParams();

        consume(TokenType::LEFT_BRACE, "Expected '{' before data body.");

        std::vector<std::shared_ptr<VarDeclStmt>> fields;
        while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
            if (match({TokenType::LET}) || match({TokenType::CONST})) {
                bool is_const = (previous().type == TokenType::CONST);
                Token field_name = consume(TokenType::IDENTIFIER, "Expected field name in data block.");
                consume(TokenType::AS, "Expected 'as' followed by a type for data field.");
                std::shared_ptr<ASTType> type_ann = type();

                if (match({TokenType::EQUAL})) {
                    throw error(previous(), "Data fields cannot have default initializers — values are provided through the auto-generated constructor.");
                }

                consume(TokenType::SEMICOLON, "Expected ';' after data field declaration.");

                fields.push_back(std::make_shared<VarDeclStmt>(field_name, type_ann, nullptr, is_const));

            } else {
                throw error(peek(), "Expected a field declaration ('let' or 'const') in data block body.");
            }
        }

        consume(TokenType::RIGHT_BRACE, "Expected '}' after data body.");
        return std::make_shared<DataStmt>(std::move(name), std::move(fields), std::move(type_params));
    }

}
