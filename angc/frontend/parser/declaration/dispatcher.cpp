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
                        throw error(func_decl->name, "An intrinsic function cannot have a body — it is replaced by a compiler builtin at compile time.", "E128");
                    }
                    func_decl->is_intrinsic = true;
                    return func_decl;
                }
                throw error(peek(), "Expected 'func' after 'intrinsic'. Only functions can be marked intrinsic.", "E129");
            }

            if (match({TokenType::FOREIGN})) {
                if (match({TokenType::FUNC})) {
                    auto func_decl = std::static_pointer_cast<FuncStmt>(function("function"));
                    if (func_decl->body) {
                        throw error(func_decl->name, "A foreign function cannot have a body — its implementation comes from an external library.", "E130");
                    }
                    func_decl->is_foreign = true;
                    return func_decl;
                }

                if (match({TokenType::DATA})) {
                    auto data_decl = std::static_pointer_cast<DataStmt>(foreignDataDeclaration());
                    if (data_decl->is_exported) {
                        throw error(data_decl->name, "A 'foreign data' declaration is an import and cannot be marked 'export'.", "E131");
                    }
                    return data_decl;
                }

                if (match({TokenType::UNION})) {
                    auto union_decl = std::static_pointer_cast<DataStmt>(foreignDataDeclaration());
                    union_decl->is_union = true;
                    if (union_decl->is_exported) {
                        throw error(union_decl->name, "A 'foreign union' declaration is an import and cannot be marked 'export'.", "E132");
                    }
                    return union_decl;
                }

                if (match({TokenType::CONST})) {
                    Token name = consume(TokenType::IDENTIFIER, "Expected constant name.", "E133");
                    consume(TokenType::AS, "A 'foreign const' requires a type annotation ('foreign const NAME as Type;').", "E134");
                    auto typeAnnotation = type();
                    consume(TokenType::SEMICOLON, "Expected ';' after foreign const declaration.", "E135");
                    auto var_decl = std::make_shared<VarDeclStmt>(std::move(name), typeAnnotation, nullptr, true);
                    var_decl->is_foreign = true;
                    return var_decl;
                }

                throw error(peek(), "Expected 'func', 'data', 'union', or 'const' after 'foreign'.", "E136");
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
                    throw error(previous(), "'attach' statements import symbols and cannot be marked 'export'.", "E137");
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
                    throw error(peek(), "Expected a declaration after 'export' (function, class, contract, trait, 'let', or 'const').", "E138");
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
