//
// Created by cv2 on 9/19/25.
//

#include "Parser.h"

namespace angara {

    std::shared_ptr<Stmt> Parser::contractDeclaration() {
        Token name = consume(TokenType::IDENTIFIER, "Expect contract name.");
        consume(TokenType::LEFT_BRACE, "Expect '{' before contract body.");

        std::vector<std::shared_ptr<ClassMember>> members;

        while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
            if (match({TokenType::PUBLIC})) {
                consume(TokenType::COLON, "Expect ':' after 'public' specifier.");
                continue;
            }
            if (match({TokenType::PRIVATE})) {
                throw error(previous(), "Cannot use 'private' access specifier in a contract. All members are implicitly public.");
            }

            if (match({TokenType::LET}) || match({TokenType::CONST})) {
                bool is_const = (previous().type == TokenType::CONST);

                // --- REVISED LOGIC: MANUALLY parse the field requirement ---
                // We do NOT call varDeclaration() here because the rules are different.

                Token field_name = consume(TokenType::IDENTIFIER, "Expect field name in contract.");

                // RULE: A contract field MUST have an explicit type.
                consume(TokenType::AS, "Expect 'as' to specify a type for a contract field.");
                std::shared_ptr<ASTType> type_ann = type();

                // RULE: A contract field CANNOT have an initializer.
                if (match({TokenType::EQUAL})) {
                    throw error(previous(), "A contract field cannot have an initializer. The signing class is responsible for initialization.");
                }

                consume(TokenType::SEMICOLON, "Expect ';' after contract field declaration.");

                // Create the VarDeclStmt with a null initializer. This is now safe
                // because the parser is no longer enforcing the "const must be initialized" rule here.
                auto field_decl = std::make_shared<VarDeclStmt>(field_name, type_ann, nullptr, is_const);
                members.push_back(std::make_shared<FieldMember>(field_decl, AccessLevel::PUBLIC));

            } else if (match({TokenType::FUNC})) {
                auto method_decl = std::static_pointer_cast<FuncStmt>(function("method"));
                if (method_decl->body) {
                    throw error(method_decl->name, "A contract method cannot have a body.");
                }
                members.push_back(std::make_shared<MethodMember>(method_decl, AccessLevel::PUBLIC));
            } else {
                throw error(peek(), "Contract body can only contain 'public:', and field ('let', 'const') or method ('func') declarations.");
            }
        }

        consume(TokenType::RIGHT_BRACE, "Expect '}' after contract body.");
        return std::make_shared<ContractStmt>(std::move(name), std::move(members));
    }

}