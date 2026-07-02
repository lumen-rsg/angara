#include "Parser.h"
namespace angara {

    std::shared_ptr<Stmt> Parser::dataDeclaration() {
        Token name = consume(TokenType::IDENTIFIER, "Expected name after 'data'.", "E120");
        std::map<std::string, Token> type_param_bounds;
        auto type_params = parseTypeParams(type_param_bounds);

        consume(TokenType::LEFT_BRACE, "Expected '{' before data body.", "E121");

        std::vector<std::shared_ptr<VarDeclStmt>> fields;
        while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
            if (match({TokenType::LET}) || match({TokenType::CONST})) {
                bool is_const = (previous().type == TokenType::CONST);
                Token field_name = consume(TokenType::IDENTIFIER, "Expected field name in data block.", "E122");
                consume(TokenType::AS, "Expected 'as' followed by a type for data field.", "E123");
                std::shared_ptr<ASTType> type_ann = type();

                if (match({TokenType::EQUAL})) {
                    throw error(previous(), "Data fields cannot have default initializers — values are provided through the auto-generated constructor.", "E124");
                }

                consume(TokenType::SEMICOLON, "Expected ';' after data field declaration.", "E125");

                fields.push_back(std::make_shared<VarDeclStmt>(field_name, type_ann, nullptr, is_const));

            } else {
                throw error(peek(), "Expected a field declaration ('let' or 'const') in data block body.", "E126");
            }
        }

        consume(TokenType::RIGHT_BRACE, "Expected '}' after data body.", "E127");
        return std::make_shared<DataStmt>(std::move(name), std::move(fields), std::move(type_params), std::move(type_param_bounds));
    }

}
