#include "Parser.h"
namespace angara {

    std::shared_ptr<Stmt> Parser::declaration() {
        try {
            bool is_exported = match({TokenType::EXPORT});
            if (match({TokenType::FUNC})) {
                auto func_decl = std::static_pointer_cast<FuncStmt>(function("function"));
                func_decl->is_exported = is_exported;
                return func_decl;
            }

            if (match({TokenType::INTRINSIC})) {
                if (match({TokenType::FUNC})) {
                    auto func_decl = std::static_pointer_cast<FuncStmt>(function("function"));
                    if (func_decl->body) {
                        throw error(func_decl->name, "An intrinsic function cannot have a body — it is replaced by a compiler builtin at compile time.");
                    }
                    func_decl->is_intrinsic = true;
                    return func_decl;
                }
                throw error(peek(), "Expected 'func' after 'intrinsic'. Only functions can be marked intrinsic.");
            }

            if (match({TokenType::FOREIGN})) {
                if (peek().type == TokenType::STRING) {
                    Token header_token = advance();
                    consume(TokenType::SEMICOLON, "Expected ';' after foreign header declaration.");
                    return std::make_shared<ForeignHeaderStmt>(header_token);
                }

                if (match({TokenType::FUNC})) {
                    auto func_decl = std::static_pointer_cast<FuncStmt>(function("function"));
                    if (func_decl->body) {
                        throw error(func_decl->name, "A foreign function cannot have a body — its implementation comes from an external library.");
                    }
                    func_decl->is_foreign = true;
                    return func_decl;
                }

                if (match({TokenType::DATA})) {
                    auto data_decl = std::static_pointer_cast<DataStmt>(dataDeclaration());
                    data_decl->is_foreign = true;
                    if (data_decl->is_exported) {
                        throw error(data_decl->name, "A 'foreign data' declaration is an import and cannot be marked 'export'.");
                    }
                    return data_decl;
                }

                throw error(peek(), "Expected 'func', 'data', or a header string (e.g., \"libc.h\") after 'foreign'.");
            }

            std::shared_ptr<Stmt> decl_stmt = nullptr;
            if (match({TokenType::CONTRACT})) {
                decl_stmt = contractDeclaration();
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
                if (is_exported) {
                    throw error(previous(), "'attach' statements import symbols and cannot be marked 'export'.");
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
                if (is_exported) {
                    throw error(peek(), "Expected a declaration after 'export' (function, class, contract, trait, 'let', or 'const').");
                }
                return statement();
            }

            return decl_stmt;

        } catch (const ParseError& error) {
            synchronize();
            return nullptr;
        }
    }

}
