//
// Created by cv2 on 9/19/25.
//

#include "Parser.h"
namespace angara {

std::shared_ptr<Stmt> Parser::classDeclaration() {
        Token name = consume(TokenType::IDENTIFIER, "Expect class name.");

        // Parse optional inheritance and traits
        std::shared_ptr<VarExpr> superclass = nullptr;
        if (match({TokenType::INHERITS})) {
            consume(TokenType::IDENTIFIER, "Expect superclass name.");
            superclass = std::make_shared<VarExpr>(previous());
        }
        std::vector<std::shared_ptr<VarExpr>> traits;
        if (match({TokenType::USES})) {
            do {
                consume(TokenType::IDENTIFIER, "Expect trait name.");
                traits.push_back(std::make_shared<VarExpr>(previous()));
            } while (match({TokenType::COMMA}));
        }

        // --- NEW: Parse optional contract signing ---
        std::vector<std::shared_ptr<VarExpr>> contracts;
        if (match({TokenType::SIGNS})) {
            do {
                consume(TokenType::IDENTIFIER, "Expect contract name.");
                contracts.push_back(std::make_shared<VarExpr>(previous()));
            } while (match({TokenType::COMMA}));
        }

        consume(TokenType::LEFT_BRACE, "Expect '{' before class body.");

        std::vector<std::shared_ptr<ClassMember>> members;
        AccessLevel current_access = AccessLevel::PRIVATE; // Classes default to private members.

        while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
            // Check for an access specifier first.
            if (match({TokenType::PUBLIC})) {
                consume(TokenType::COLON, "Expect ':' after 'public' specifier.");
                current_access = AccessLevel::PUBLIC;
                continue; // Continue to the next line/member
            }
            if (match({TokenType::PRIVATE})) {
                consume(TokenType::COLON, "Expect ':' after 'private' specifier.");
                current_access = AccessLevel::PRIVATE;
                continue; // Continue to the next line/member
            }

            // If not a specifier, it must be a member declaration.
            bool is_static = match({TokenType::STATIC});

            if (match({TokenType::LET}) || match({TokenType::CONST})) {
                bool is_const = (previous().type == TokenType::CONST);
                // We just consumed 'let' or 'const'. Now call the helper that
                // parses the rest of the declaration.
                auto field_decl = std::static_pointer_cast<VarDeclStmt>(varDeclaration(is_const));
                field_decl->is_static = is_static;
                members.push_back(std::make_shared<FieldMember>(field_decl, current_access));
            }  else if (match({TokenType::FUNC})) {
                auto method_decl = std::static_pointer_cast<FuncStmt>(function("method"));
                method_decl->is_static = is_static;
                members.push_back(std::make_shared<MethodMember>(method_decl, current_access));
            } else {
                throw error(peek(), "Class body can only contain access specifiers ('public:', 'private:') and member declarations ('let', 'func').");
            }
        }

        consume(TokenType::RIGHT_BRACE, "Expect '}' after class body.");

        return std::make_shared<ClassStmt>(std::move(name), std::move(superclass), std::move(contracts), std::move(traits), std::move(members));
    }

}