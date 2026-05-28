#include "Parser.h"

namespace angara {

    std::shared_ptr<Stmt> Parser::contractDeclaration() {
        Token name = consume(TokenType::IDENTIFIER, "Expected contract name after 'contract'.", "E139");
        consume(TokenType::LEFT_BRACE, "Expected '{' before contract body.", "E140");

        std::vector<std::shared_ptr<ClassMember>> members;

        while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
            if (match({TokenType::PUBLIC})) {
                consume(TokenType::COLON, "Expected ':' after 'public' specifier.", "E141");
                continue;
            }
            if (match({TokenType::PRIVATE})) {
                throw error(previous(), "'private' is not allowed in a contract — all members are implicitly public.", "E142");
            }

            if (match({TokenType::LET}) || match({TokenType::CONST})) {
                bool is_const = (previous().type == TokenType::CONST);

                Token field_name = consume(TokenType::IDENTIFIER, "Expected field name in contract.", "E143");
                consume(TokenType::AS, "Expected 'as' followed by a type for contract field '%s'.", "E144");
                std::shared_ptr<ASTType> type_ann = type();

                if (match({TokenType::EQUAL})) {
                    throw error(previous(), "Contract fields cannot have initializers — the implementing class is responsible for providing values.", "E145");
                }

                consume(TokenType::SEMICOLON, "Expected ';' after contract field declaration.", "E146");
                auto field_decl = std::make_shared<VarDeclStmt>(field_name, type_ann, nullptr, is_const);
                members.push_back(std::make_shared<FieldMember>(field_decl, AccessLevel::PUBLIC));

            } else if (match({TokenType::FUNC})) {
                auto method_decl = std::static_pointer_cast<FuncStmt>(function("method"));
                if (method_decl->body) {
                    throw error(method_decl->name, "Contract methods cannot have a body — they declare a signature that the implementing class must fulfill.", "E147");
                }
                members.push_back(std::make_shared<MethodMember>(method_decl, AccessLevel::PUBLIC));
            } else {
                throw error(peek(), "Expected a field declaration ('let', 'const') or method signature ('func') in contract body.", "E148");
            }
        }

        consume(TokenType::RIGHT_BRACE, "Expected '}' after contract body.", "E149");
        return std::make_shared<ContractStmt>(std::move(name), std::move(members));
    }

}
