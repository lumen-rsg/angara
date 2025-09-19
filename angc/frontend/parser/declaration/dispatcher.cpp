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