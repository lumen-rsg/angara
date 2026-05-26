#include "Parser.h"

namespace angara {

    std::shared_ptr<Stmt> Parser::contractDeclaration() {
        Token name = consume(TokenType::IDENTIFIER, "Expected contract name after 'contract'.");
        consume(TokenType::LEFT_BRACE, "Expected '{' before contract body.");

        std::vector<std::shared_ptr<ClassMember>> members;

        while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
            if (match({TokenType::PUBLIC})) {
                consume(TokenType::COLON, "Expected ':' after 'public' specifier.");
                continue;
            }
            if (match({TokenType::PRIVATE})) {
                throw error(previous(), "'private' is not allowed in a contract — all members are implicitly public.");
            }

            if (match({TokenType::LET}) || match({TokenType::CONST})) {
                bool is_const = (previous().type == TokenType::CONST);

                Token field_name = consume(TokenType::IDENTIFIER, "Expected field name in contract.");
                consume(TokenType::AS, "Expected 'as' followed by a type for contract field '%s'.");
                std::shared_ptr<ASTType> type_ann = type();

                if (match({TokenType::EQUAL})) {
                    throw error(previous(), "Contract fields cannot have initializers — the implementing class is responsible for providing values.");
                }

                consume(TokenType::SEMICOLON, "Expected ';' after contract field declaration.");
                auto field_decl = std::make_shared<VarDeclStmt>(field_name, type_ann, nullptr, is_const);
                members.push_back(std::make_shared<FieldMember>(field_decl, AccessLevel::PUBLIC));

            } else if (match({TokenType::FUNC})) {
                auto method_decl = std::static_pointer_cast<FuncStmt>(function("method"));
                if (method_decl->body) {
                    throw error(method_decl->name, "Contract methods cannot have a body — they declare a signature that the implementing class must fulfill.");
                }
                members.push_back(std::make_shared<MethodMember>(method_decl, AccessLevel::PUBLIC));
            } else {
                throw error(peek(), "Expected a field declaration ('let', 'const') or method signature ('func') in contract body.");
            }
        }

        consume(TokenType::RIGHT_BRACE, "Expected '}' after contract body.");
        return std::make_shared<ContractStmt>(std::move(name), std::move(members));
    }

}
