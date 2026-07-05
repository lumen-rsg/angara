#include "Parser.h"

namespace angara {

    std::shared_ptr<Expr> Parser::parseMatchPattern() {
        if (peek().type == TokenType::IDENTIFIER && peek().lexeme == "_") {
            return std::make_shared<VarExpr>(advance());
        }
        return call();
    }


    std::shared_ptr<Expr> Parser::matchExpression() {
        Token match_keyword = previous();
        consume(TokenType::LEFT_PAREN, "Expected '(' after 'match'.", "E219");
        auto condition = expression();
        consume(TokenType::RIGHT_PAREN, "Expected ')' after 'match' condition.", "E220");
        consume(TokenType::LEFT_BRACE, "Expected '{' to begin 'match' body.", "E221");

        std::vector<MatchCase> cases;

        while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
            consume(TokenType::CASE, "Expected 'case' to begin a match arm.", "E222");

            // --- Parse or-patterns: case pat1 [| pat2 ...] ---
            std::vector<std::shared_ptr<Expr>> patterns;
            std::vector<Token> variables;
            std::vector<std::vector<Token>> alt_variables;  // per-alternative bindings
            bool has_wildcard_in_or = false;

            // Parse the first pattern atom
            {
                std::shared_ptr<Expr> atom = primary();

                // Qualified constructor path: WebEvent.KeyPress
                while (match({TokenType::DOT})) {
                    Token op = previous();
                    Token name = consume(TokenType::IDENTIFIER,
                        "Expected property name after '.' in match pattern.", "E223");
                    atom = std::make_shared<GetExpr>(std::move(atom), op, std::move(name));
                }

                // Payload bindings: (var1, var2, ...)
                std::vector<Token> first_vars;
                if (match({TokenType::LEFT_PAREN})) {
                    if (!check(TokenType::RIGHT_PAREN)) {
                        do {
                            Token var = consume(TokenType::IDENTIFIER,
                                "Expected a variable name to bind the enum variant's payload.", "E224");
                            first_vars.push_back(var);
                        } while (match({TokenType::COMMA}));
                    }
                    consume(TokenType::RIGHT_PAREN,
                        "Expected ')' after pattern variable(s).", "E225");
                }
                variables = first_vars;
                alt_variables.push_back(std::move(first_vars));

                if (auto ve = std::dynamic_pointer_cast<const VarExpr>(atom)) {
                    if (ve->name.lexeme == "_") has_wildcard_in_or = true;
                }
                patterns.push_back(atom);
            }

            // Parse additional or-pattern alternatives: | pat2 | pat3 ...
            while (match({TokenType::PIPE})) {
                std::shared_ptr<Expr> atom = primary();

                while (match({TokenType::DOT})) {
                    Token op = previous();
                    Token name = consume(TokenType::IDENTIFIER,
                        "Expected property name after '.' in match pattern.", "E223");
                    atom = std::make_shared<GetExpr>(std::move(atom), op, std::move(name));
                }

                // Or-pattern alternatives with bindings must match the first alternative
                std::vector<Token> alt_vars;
                if (match({TokenType::LEFT_PAREN})) {
                    if (!check(TokenType::RIGHT_PAREN)) {
                        do {
                            Token var = consume(TokenType::IDENTIFIER,
                                "Expected a variable name to bind the enum variant's payload.", "E224");
                            alt_vars.push_back(var);
                        } while (match({TokenType::COMMA}));
                    }
                    consume(TokenType::RIGHT_PAREN,
                        "Expected ')' after pattern variable(s).", "E225");

                    // All alternatives in an or-pattern must bind the same variables
                    if (variables.empty()) {
                        variables = alt_vars;
                    } else if (alt_vars.size() == variables.size()) {
                        for (size_t i = 0; i < variables.size(); ++i) {
                            if (variables[i].lexeme != alt_vars[i].lexeme) {
                                error(alt_vars[i],
                                    "Or-pattern alternatives must bind the same variable names. "
                                    "Expected '" + variables[i].lexeme + "' but got '" + alt_vars[i].lexeme + "'.",
                                    "E400");
                                break;
                            }
                        }
                    } else {
                        error(peek(),
                            "Or-pattern alternatives must bind the same number of variables. "
                            "Expected " + std::to_string(variables.size()) + " but got " +
                            std::to_string(alt_vars.size()) + ".",
                            "E400");
                    }
                }
                alt_variables.push_back(std::move(alt_vars));

                if (auto ve = std::dynamic_pointer_cast<const VarExpr>(atom)) {
                    if (ve->name.lexeme == "_") has_wildcard_in_or = true;
                }
                patterns.push_back(atom);
            }

            // Or-patterns cannot mix wildcard with other patterns
            if (has_wildcard_in_or && patterns.size() > 1) {
                error(patterns[0] ? dynamic_cast<const VarExpr*>(patterns[0].get()) ?
                      dynamic_cast<const VarExpr*>(patterns[0].get())->name :
                      Token{} : Token{},
                    "Wildcard '_' cannot be combined with other patterns in an or-pattern. "
                    "Use '_' as its own case arm.",
                    "E401");
            }

            // --- Parse optional guard: if guard_expr ---
            std::optional<std::shared_ptr<Expr>> guard = std::nullopt;
            if (match({TokenType::IF})) {
                guard = expression();
            }

            // --- Parse body ---
            consume(TokenType::COLON, "Expected ':' after match pattern.", "E226");

            std::shared_ptr<Expr> body;
            if (match({TokenType::LEFT_BRACE})) {
                body = expression();
                consume(TokenType::RIGHT_BRACE, "Expected '}' after match case body.", "E227");
            } else {
                body = expression();
            }

            cases.push_back({std::move(patterns), std::move(variables), std::move(alt_variables), guard, body});

            if (!check(TokenType::RIGHT_BRACE)) {
                consume(TokenType::COMMA,
                    "Expected ',' between match cases, or '}' to close 'match'.", "E228");
            }
        }

        consume(TokenType::RIGHT_BRACE, "Expected '}' to close 'match' body.", "E229");
        return std::make_shared<MatchExpr>(match_keyword, condition, std::move(cases));
    }

}
