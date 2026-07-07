#include "Parser.h"
namespace angara {

    std::shared_ptr<Stmt> Parser::declaration() {
        try {
            // RT-1: parse @on_throw(<value>) annotation for foreign funcs.
            // SIMD-2: parse @inline annotation for functions.
            // v5: parse @consumes / @escape annotations for ownership semantics.
            // M11: parse @sendable annotation for thread-safe types.
            std::optional<int64_t> pending_on_throw;
            bool pending_inline = false;
            bool pending_sendable = false;
            bool pending_unsendable = false;
            bool pending_sync = false;
            bool pending_unsync = false;
            std::set<int> pending_consumes;
            std::set<int> pending_escapes;

            // Helper: parse comma-separated integer list inside parens, e.g. "(0, 2)"
            auto parse_param_index_list = [&](std::set<int>& out) {
                consume(TokenType::LEFT_PAREN, "Expected '(' after annotation.", "E393");
                if (!check(TokenType::RIGHT_PAREN)) {
                    do {
                        Token val = consume(TokenType::NUMBER_INT,
                            "Expected integer parameter index.", "E394");
                        int idx;
                        try {
                            idx = std::stoi(val.lexeme);
                        } catch (const std::out_of_range&) {
                            throw error(val, "Parameter index '" + val.lexeme + "' is too large.", "E394");
                        } catch (const std::invalid_argument&) {
                            throw error(val, "Invalid parameter index '" + val.lexeme + "'.", "E394");
                        }
                        out.insert(idx);
                    } while (match({TokenType::COMMA}));
                }
                consume(TokenType::RIGHT_PAREN, "Expected ')' after parameter list.", "E395");
            };

            while (check(TokenType::AT_SIGN)) {
                int saved = m_current;
                advance(); // consume '@'
                Token ann = peek();
                if (ann.type == TokenType::IDENTIFIER && ann.lexeme == "inline") {
                    advance();
                    pending_inline = true;
                } else if (ann.type == TokenType::IDENTIFIER && ann.lexeme == "on_throw") {
                    advance();
                    consume(TokenType::LEFT_PAREN, "Expected '(' after '@on_throw'.", "E393");
                    bool neg = match({TokenType::MINUS});
                    Token val = consume(TokenType::NUMBER_INT, "Expected an integer value in @on_throw(...).", "E394");
                    consume(TokenType::RIGHT_PAREN, "Expected ')' after @on_throw value.", "E395");
                    int64_t v;
                    try {
                        v = std::stoll(val.lexeme);
                    } catch (const std::out_of_range&) {
                        throw error(val, "Value '" + val.lexeme + "' in @on_throw is too large.", "E394");
                    } catch (const std::invalid_argument&) {
                        throw error(val, "Invalid value '" + val.lexeme + "' in @on_throw.", "E394");
                    }
                    pending_on_throw = neg ? -v : v;
                } else if (ann.type == TokenType::IDENTIFIER && ann.lexeme == "consumes") {
                    advance();
                    parse_param_index_list(pending_consumes);
                } else if (ann.type == TokenType::IDENTIFIER && ann.lexeme == "escape") {
                    advance();
                    parse_param_index_list(pending_escapes);
                } else if (ann.type == TokenType::IDENTIFIER && ann.lexeme == "sendable") {
                    advance();
                    pending_sendable = true;
                } else if (ann.type == TokenType::IDENTIFIER && ann.lexeme == "unsendable") {
                    advance();
                    pending_unsendable = true;
                } else if (ann.type == TokenType::IDENTIFIER && ann.lexeme == "sync") {
                    advance();
                    pending_sync = true;
                } else if (ann.type == TokenType::IDENTIFIER && ann.lexeme == "unsync") {
                    advance();
                    pending_unsync = true;
                } else {
                    // Not a recognized annotation — restore and break out.
                    m_current = saved;
                    break;
                }
            }

            bool is_exported = match({TokenType::EXPORT});
            // LIB-4: async func — asynchronous function returning Future<T>
            bool is_async = match({TokenType::ASYNC});
            // LANG-17: if 'func' is immediately followed by '(' (no name), it's
            // a lambda expression-statement (IIFE: func(){...}()), not a
            // declaration. Fall through to statement() so the lambda parser
            // (primaryExpression) handles it.
            if (check(TokenType::FUNC) && m_current + 1 < (int)m_tokens.size() &&
                m_tokens[m_current + 1].type == TokenType::LEFT_PAREN) {
                if (is_async) {
                    throw error(previous(), "'async' must be followed by a named function declaration, not an anonymous function expression.", "E415");
                }
                return statement();
            }
            if (match({TokenType::FUNC})) {
                auto func_decl = std::static_pointer_cast<FuncStmt>(function("function"));
                func_decl->is_exported = is_exported;
                func_decl->is_async = is_async;
                func_decl->is_inline = pending_inline;
                func_decl->consumes_params = std::move(pending_consumes);
                func_decl->escape_params = std::move(pending_escapes);
                return func_decl;
            }

            if (is_async) {
                throw error(previous(), "Expected 'func' after 'async'.", "E416");
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
                    func_decl->is_exported = is_exported;
                    func_decl->is_foreign = true;
                    if (pending_on_throw) {
                        func_decl->on_throw_value = pending_on_throw;
                    }
                    func_decl->consumes_params = std::move(pending_consumes);
                    func_decl->escape_params = std::move(pending_escapes);
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
                std::static_pointer_cast<ClassStmt>(decl_stmt)->is_sendable = pending_sendable;
                std::static_pointer_cast<ClassStmt>(decl_stmt)->is_unsendable = pending_unsendable;
                std::static_pointer_cast<ClassStmt>(decl_stmt)->is_sync = pending_sync;
                std::static_pointer_cast<ClassStmt>(decl_stmt)->is_unsync = pending_unsync;
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
                data_decl->is_sendable = pending_sendable;
                data_decl->is_unsendable = pending_unsendable;
                data_decl->is_sync = pending_sync;
                data_decl->is_unsync = pending_unsync;
                return data_decl;
            } else if (match({TokenType::DATA})) {
                auto data_decl = std::static_pointer_cast<DataStmt>(dataDeclaration());
                data_decl->is_exported = is_exported;
                data_decl->is_sendable = pending_sendable;
                data_decl->is_unsendable = pending_unsendable;
                data_decl->is_sync = pending_sync;
                data_decl->is_unsync = pending_unsync;
                return data_decl;
            } else if (match({TokenType::ENUM})) {
                auto enum_decl = std::static_pointer_cast<EnumStmt>(enumDeclaration());
                enum_decl->is_exported = is_exported;
                return enum_decl;
            } else if (match({TokenType::TYPE})) {
                auto alias_decl = std::static_pointer_cast<TypeAliasStmt>(typeAliasDeclaration());
                alias_decl->is_exported = is_exported;
                return alias_decl;
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
