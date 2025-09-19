//
// Created by cv2 on 9/19/25.
//
#include "Parser.h"
namespace angara {

    std::shared_ptr<Stmt> Parser::dataDeclaration() {
        Token name = consume(TokenType::IDENTIFIER, "Expect data block name.");
        consume(TokenType::LEFT_BRACE, "Expect '{' before data block body.");

        std::vector<std::shared_ptr<VarDeclStmt>> fields;
        while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
            if (match({TokenType::LET}) || match({TokenType::CONST})) {
                bool is_const = (previous().type == TokenType::CONST);

                // --- Manually parse the field, do not use varDeclaration ---
                Token field_name = consume(TokenType::IDENTIFIER, "Expect field name in data block.");

                // Rule 1: A data field MUST have an explicit type.
                consume(TokenType::AS, "Expect 'as' to specify a type for a data block field.");
                std::shared_ptr<ASTType> type_ann = type();

                // Rule 2: A data field CANNOT have a default initializer.
                if (match({TokenType::EQUAL})) {
                    throw error(previous(), "A 'data' block field cannot have a default initializer. Values are provided via the constructor.");
                }

                consume(TokenType::SEMICOLON, "Expect ';' after data block field declaration.");

                // Create the VarDeclStmt with a null initializer.
                fields.push_back(std::make_shared<VarDeclStmt>(field_name, type_ann, nullptr, is_const));

            } else {
                throw error(peek(), "A 'data' block body can only contain 'let' or 'const' field declarations.");
            }
        }

        consume(TokenType::RIGHT_BRACE, "Expect '}' after data block body.");
        return std::make_shared<DataStmt>(std::move(name), std::move(fields));
    }

}