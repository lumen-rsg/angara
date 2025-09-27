//
// Created by cv2 on 9/19/25.
//
#include "Parser.h"
namespace angara {

    // declaration → letDecl | statement
        // declaration → "export"? (class_decl | trait_decl | func_decl | var_decl) | statement
    std::shared_ptr<Stmt> Parser::declaration() {
        try {
            // Look for an optional 'export' keyword first.
            bool is_exported = match({TokenType::EXPORT});

            // Now, check for the actual declaration type.
            // We handle `func` separately as it can appear with or without `export`.
            if (match({TokenType::FUNC})) {
                auto func_decl = std::static_pointer_cast<FuncStmt>(function("function"));
                func_decl->is_exported = is_exported;
                return func_decl;
            }

            if (match({TokenType::FOREIGN})) {
                // --- REVISED, CORRECTED LOGIC ---

                // Case 1: `foreign "header.h";`
                if (peek().type == TokenType::STRING) {
                    Token header_token = advance();
                    consume(TokenType::SEMICOLON, "Expect ';' after a foreign header declaration.");
                    return std::make_shared<ForeignHeaderStmt>(header_token);
                }

                // Case 2: `foreign func ...`
                if (match({TokenType::FUNC})) {
                    auto func_decl = std::static_pointer_cast<FuncStmt>(function("function"));
                    if (func_decl->body) {
                        throw error(func_decl->name, "A foreign function declaration cannot have a body.");
                    }
                    func_decl->is_foreign = true;
                    return func_decl;
                }

                // Case 3: `foreign data ...`
                if (match({TokenType::DATA})) {
                    auto data_decl = std::static_pointer_cast<DataStmt>(dataDeclaration());
                    data_decl->is_foreign = true;
                    if (data_decl->is_exported) {
                        throw error(data_decl->name, "A 'foreign data' declaration is an import and cannot be exported.");
                    }
                    return data_decl;
                }

                // If none of the above, it's an error.
                throw error(peek(), "Expect 'func', 'data', or a header string after 'foreign'.");
            }

            std::shared_ptr<Stmt> decl_stmt = nullptr;
            if (match({TokenType::CONTRACT})) {
                decl_stmt = contractDeclaration();
                // We need to downcast to set the flag.
                std::static_pointer_cast<ContractStmt>(decl_stmt)->is_exported = is_exported;
            } else if (match({TokenType::CLASS})) {
                decl_stmt = classDeclaration();
                std::static_pointer_cast<ClassStmt>(decl_stmt)->is_exported = is_exported;
            } else if (match({TokenType::TRAIT})) {
                decl_stmt = traitDeclaration();
                std::static_pointer_cast<TraitStmt>(decl_stmt)->is_exported = is_exported;
            } else if (match({TokenType::CONST})) {
                decl_stmt = varDeclaration(true);
                std::static_pointer_cast<VarDeclStmt>(decl_stmt)->is_exported = is_exported;
            } else if (match({TokenType::LET})) {
                decl_stmt = varDeclaration(false);
                std::static_pointer_cast<VarDeclStmt>(decl_stmt)->is_exported = is_exported;
            } else if (match({TokenType::ATTACH})) {
                // 'attach' cannot be exported, so if 'is_exported' is true, it's an error.
                if (is_exported) {
                    throw error(previous(), "'attach' statements cannot be exported.");
                }
                return attachStatement();
            } else if (match({TokenType::DATA})) {
                auto data_decl = std::static_pointer_cast<DataStmt>(dataDeclaration());
                data_decl->is_exported = is_exported;
                return data_decl;
            } else if (match({TokenType::ENUM})) {
                auto enum_decl = std::static_pointer_cast<EnumStmt>(enumDeclaration());
                enum_decl->is_exported = is_exported;
                return enum_decl;
            } else {
                // If we saw 'export' but not a valid declaration that can follow it, it's an error.
                if (is_exported) {
                    throw error(peek(), "Expect a class, contract, trait, function, or variable declaration after 'export'.");
                }
                // Otherwise, it's just a regular statement.
                return statement();
            }

            return decl_stmt;

        } catch (const ParseError& error) {
            synchronize();
            return nullptr;
        }
    }

}