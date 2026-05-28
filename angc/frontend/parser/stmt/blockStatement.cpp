#include "Parser.h"
namespace angara {

    static bool is_terminator(const std::shared_ptr<Stmt>& stmt) {
        return dynamic_cast<const ReturnStmt*>(stmt.get()) != nullptr ||
               dynamic_cast<const ThrowStmt*>(stmt.get()) != nullptr ||
               dynamic_cast<const BreakStmt*>(stmt.get()) != nullptr;
    }
    static Token get_terminator_token(const std::shared_ptr<Stmt>& stmt) {
        if (auto* ret = dynamic_cast<const ReturnStmt*>(stmt.get())) return ret->keyword;
        if (auto* thr = dynamic_cast<const ThrowStmt*>(stmt.get())) return thr->keyword;
        if (auto* brk = dynamic_cast<const BreakStmt*>(stmt.get())) return brk->keyword;
        return Token();
    }

    std::vector<std::shared_ptr<Stmt>> Parser::block() {
        std::vector<std::shared_ptr<Stmt>> statements;
        bool hit_terminator = false;
        Token terminator_token;

        while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
            if (hit_terminator) {
                warning(peek(), "Unreachable code after '" + terminator_token.lexeme + "' statement.");
                hit_terminator = false;
            }

            auto stmt = declaration();
            if (!stmt) continue;

            if (is_terminator(stmt)) {
                hit_terminator = true;
                terminator_token = get_terminator_token(stmt);
            }

            statements.push_back(stmt);
        }

        consume(TokenType::RIGHT_BRACE, "Expected '}' to close block.", "E176");
        return statements;
    }

}
