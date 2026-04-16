//
// Created by cv2 on 9/19/25.
//
#include "Parser.h"
namespace angara {

    // Helper: check if a statement is a "terminator" (return, throw, break)
    static bool is_terminator(const std::shared_ptr<Stmt>& stmt) {
        return dynamic_cast<const ReturnStmt*>(stmt.get()) != nullptr ||
               dynamic_cast<const ThrowStmt*>(stmt.get()) != nullptr ||
               dynamic_cast<const BreakStmt*>(stmt.get()) != nullptr;
    }

    // Helper: get the keyword token from a terminator statement
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
            // Warn if we already saw a terminator and there's more code
            if (hit_terminator) {
                warning(peek(), "Unreachable code after '" + terminator_token.lexeme + "' statement.");
                hit_terminator = false; // Only warn once per terminator
            }

            auto stmt = declaration();

            if (is_terminator(stmt)) {
                hit_terminator = true;
                terminator_token = get_terminator_token(stmt);
            }

            statements.push_back(stmt);
        }

        consume(TokenType::RIGHT_BRACE, "Expect '}' after block.");
        return statements;
    }

}