// angara/angc/frontend/parser/expr/match.cpp

#include "Parser.h"

namespace angara {

// Helper to parse the base of a pattern (e.g., `_`, `Color.Green`, or `WebEvent.KeyPress`)
// It stops BEFORE the optional `(variable)` part.
    std::shared_ptr<Expr> Parser::parseMatchPattern() {
        // A pattern can be a wildcard `_`. We parse it as a VarExpr.
        if (peek().type == TokenType::IDENTIFIER && peek().lexeme == "_") {
            return std::make_shared<VarExpr>(advance());
        }

        // Otherwise, a pattern is like a `call()` expression but without arguments.
        // It can be a simple identifier or a series of property accesses.
        // We can reuse the `call()` parser as it correctly handles `a.b.c`.
        return call();
    }


    std::shared_ptr<Expr> Parser::matchExpression() {
        Token match_keyword = previous();
        consume(TokenType::LEFT_PAREN, "Expect '(' after 'match'.");
        auto condition = expression();
        consume(TokenType::RIGHT_PAREN, "Expect ')' after match condition.");
        consume(TokenType::LEFT_BRACE, "Expect '{' to begin match body.");

        std::vector<MatchCase> cases;

        // --- REVISED LOOP LOGIC ---
        while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
            consume(TokenType::CASE, "Expect 'case' for each match arm.");

            // (The pattern parsing logic remains the same as the last fix)
            std::shared_ptr<Expr> pattern = primary();
            while (match({TokenType::DOT})) {
                Token op = previous();
                Token name = consume(TokenType::IDENTIFIER, "Expect property name in pattern.");
                pattern = std::make_shared<GetExpr>(std::move(pattern), op, std::move(name));
            }
            std::optional<Token> variable = std::nullopt;
            if (match({TokenType::LEFT_PAREN})) {
                variable = consume(TokenType::IDENTIFIER, "Expect a variable name to bind to the enum variant's value.");
                consume(TokenType::RIGHT_PAREN, "Expect ')' after pattern variable.");
            }

            consume(TokenType::COLON, "Expect ':' after match pattern.");

            std::shared_ptr<Expr> body;
            if (match({TokenType::LEFT_BRACE})) {
                body = expression();
                consume(TokenType::RIGHT_BRACE, "Expect '}' after match case body.");
            } else {
                body = expression();
            }

            cases.push_back({pattern, variable, body});

            // If the next token is not a '}', we expect a comma.
            // This makes the comma a separator, not a terminator.
            if (!check(TokenType::RIGHT_BRACE)) {
                consume(TokenType::COMMA, "Expect ',' to separate match cases.");
            }
        }
        // --- END REVISION ---

        consume(TokenType::RIGHT_BRACE, "Expect '}' after match body.");
        return std::make_shared<MatchExpr>(match_keyword, condition, std::move(cases));
    }

}