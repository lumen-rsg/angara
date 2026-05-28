#include "Parser.h"
namespace angara {

std::shared_ptr<Stmt> Parser::classDeclaration() {
        Token name = consume(TokenType::IDENTIFIER, "Expected class name after 'class'.", "E150");
        std::shared_ptr<VarExpr> superclass = nullptr;
        if (match({TokenType::INHERITS})) {
            Token super_name = consume(TokenType::IDENTIFIER, "Expected superclass name after 'inherits'.", "E151");
            superclass = std::make_shared<VarExpr>(super_name);
        }
        std::vector<std::shared_ptr<VarExpr>> traits;
        if (match({TokenType::USES})) {
            do {
                Token trait_name = consume(TokenType::IDENTIFIER, "Expected trait name after 'uses'.", "E152");
                traits.push_back(std::make_shared<VarExpr>(trait_name));
            } while (match({TokenType::COMMA}));
        }
        std::vector<std::shared_ptr<VarExpr>> contracts;
        if (match({TokenType::SIGNS})) {
            do {
                Token contract_name = consume(TokenType::IDENTIFIER, "Expected contract name after 'signs'.", "E153");
                contracts.push_back(std::make_shared<VarExpr>(contract_name));
            } while (match({TokenType::COMMA}));
        }

        consume(TokenType::LEFT_BRACE, "Expected '{' before class body.", "E154");

        std::vector<std::shared_ptr<ClassMember>> members;
        AccessLevel current_access = AccessLevel::PRIVATE;

        while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
            if (match({TokenType::PUBLIC})) {
                consume(TokenType::COLON, "Expected ':' after 'public' access specifier.", "E155");
                current_access = AccessLevel::PUBLIC;
                continue;
            }
            if (match({TokenType::PRIVATE})) {
                consume(TokenType::COLON, "Expected ':' after 'private' access specifier.", "E156");
                current_access = AccessLevel::PRIVATE;
                continue;
            }

            bool is_static = match({TokenType::STATIC});

            if (match({TokenType::LET}) || match({TokenType::CONST})) {
                bool is_const = (previous().type == TokenType::CONST);
                auto field_decl = std::static_pointer_cast<VarDeclStmt>(varDeclaration(is_const));
                field_decl->is_static = is_static;
                members.push_back(std::make_shared<FieldMember>(field_decl, current_access));
            }  else if (match({TokenType::FUNC})) {
                auto method_decl = std::static_pointer_cast<FuncStmt>(function("method"));
                method_decl->is_static = is_static;
                members.push_back(std::make_shared<MethodMember>(method_decl, current_access));
            } else {
                throw error(peek(), "Expected a member declaration ('let', 'const', 'func') or access specifier ('public:', 'private:') in class body.", "E157");
            }
        }

        consume(TokenType::RIGHT_BRACE, "Expected '}' after class body.", "E158");

        return std::make_shared<ClassStmt>(std::move(name), std::move(superclass), std::move(contracts), std::move(traits), std::move(members));
    }

}
