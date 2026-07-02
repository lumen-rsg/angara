#include "Parser.h"
namespace angara {

    std::shared_ptr<Stmt> Parser::declaration() {
        try {
            // RT-1: parse @on_throw(<value>) annotation for foreign funcs.
            std::optional<int64_t> pending_on_throw;
            if (check(TokenType::AT_SIGN)) {
                int saved = m_current;
                advance(); // consume '@'
                Token ann = peek();
                if (ann.type == TokenType::IDENTIFIER && ann.lexeme == "on_throw") {
                    advance();
                    consume(TokenType::LEFT_PAREN, "Expected '(' after '@on_throw'.", "E393");
                    bool neg = match({TokenType::MINUS});
                    Token val = consume(TokenType::NUMBER_INT, "Expected an integer value in @on_throw(...).", "E394");
                    consume(TokenType::RIGHT_PAREN, "Expected ')' after @on_throw value.", "E395");
                    int64_t v = std::stoll(val.lexeme);
                    pending_on_throw = neg ? -v : v;
                } else {
                    // Not @on_throw — restore and let other handlers deal with it.
                    m_current = saved;
                }
            }

            bool is_exported = match({TokenType::EXPORT});
            // LANG-17: if 'func' is immediately followed by '(' (no name), it's
            // a lambda expression-statement (IIFE: func(){...}()), not a
            // declaration. Fall through to statement() so the lambda parser
            // (primaryExpression) handles it.
            if (check(TokenType::FUNC) && m_current + 1 < (int)m_tokens.size() &&
                m_tokens[m_current + 1].type == TokenType::LEFT_PAREN) {
                return statement();
            }
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
                    if (pending_on_throw) {
                        func_decl->on_throw_value = pending_on_throw;
                    }
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
            } else if (match({TokenType::OWNED})) {
                auto data_decl = std::static_pointer_cast<DataStmt>(dataDeclaration());
                data_decl->is_owned = true;
                data_decl->is_exported = is_exported;
                return data_decl;
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
