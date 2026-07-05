#include "Parser.h"

namespace angara {

    // Parse a single match pattern, which may be:
    //   - Wildcard: _
    //   - Variable binding: v   (in sub-pattern context)
    //   - Literal: 42, "hello", true, 'a'
    //   - Constructor: Name
    //   - Nested constructor: Name(pat1, pat2, ...)
    std::shared_ptr<Expr> Parser::parseMatchPattern() {
        // Wildcard
        if (peek().type == TokenType::IDENTIFIER && peek().lexeme == "_") {
            return std::make_shared<VarExpr>(advance());
        }

        // Literal patterns (for value matches)
        if (peek().type == TokenType::NUMBER_INT ||
            peek().type == TokenType::NUMBER_FLOAT ||
            peek().type == TokenType::STRING ||
            peek().type == TokenType::RAW_STRING ||
            peek().type == TokenType::BYTE_STRING ||
            peek().type == TokenType::CHAR ||
            peek().type == TokenType::TRUE ||
            peek().type == TokenType::FALSE ||
            peek().type == TokenType::NIL) {
            return std::make_shared<Literal>(advance());
        }

        // Constructor or simple variable pattern
        std::shared_ptr<Expr> atom = primary();

        // Qualified constructor path: Module.Variant
        while (match({TokenType::DOT})) {
            Token op = previous();
            Token name = consume(TokenType::IDENTIFIER,
                "Expected property name after '.' in match pattern.", "E223");
            atom = std::make_shared<GetExpr>(std::move(atom), op, std::move(name));
        }

        // Nested sub-patterns: (pat1, pat2, ...)
        if (match({TokenType::LEFT_PAREN})) {
            std::vector<std::shared_ptr<Expr>> subpatterns;
            std::vector<Token> bindings;

            if (!check(TokenType::RIGHT_PAREN)) {
                do {
                    // Distinguish variable binding from nested constructor:
                    // IDENTIFIER followed by '(' or '.' → constructor pattern
                    // IDENTIFIER followed by ',' or ')' → variable binding
                    if (peek().type == TokenType::IDENTIFIER) {
                        TokenType next = peekNext().type;
                        bool is_constructor = (next == TokenType::LEFT_PAREN ||
                                               next == TokenType::DOT);

                        if (is_constructor) {
                            subpatterns.push_back(parseMatchPattern());
                        } else {
                            Token var = consume(TokenType::IDENTIFIER,
                                "Expected a variable name or pattern.", "E224");
                            bindings.push_back(var);
                            subpatterns.push_back(std::make_shared<VarExpr>(var));
                        }
                    } else if (peek().type == TokenType::IDENTIFIER && peek().lexeme == "_") {
                        // Wildcard in sub-pattern
                        subpatterns.push_back(std::make_shared<VarExpr>(advance()));
                    } else {
                        // Literal or other pattern
                        subpatterns.push_back(parseMatchPattern());
                    }
                } while (match({TokenType::COMMA}));
            }
            consume(TokenType::RIGHT_PAREN,
                "Expected ')' after pattern arguments.", "E225");

            return std::make_shared<NestedPattern>(
                std::move(atom), std::move(subpatterns), std::move(bindings));
        }

        return atom;
    }


    static void collectBindings(const std::shared_ptr<Expr>& pat, std::vector<Token>& out) {
        if (auto* np = dynamic_cast<const NestedPattern*>(pat.get())) {
            for (auto& b : np->bindings) out.push_back(b);
            for (auto& sp : np->subpatterns) collectBindings(sp, out);
        }
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

            // Parse the first pattern
            {
                std::shared_ptr<Expr> first_pat = parseMatchPattern();
                std::vector<Token> first_vars;
                collectBindings(first_pat, first_vars);
                variables = first_vars;
                alt_variables.push_back(std::move(first_vars));

                if (auto ve = std::dynamic_pointer_cast<const VarExpr>(first_pat)) {
                    if (ve->name.lexeme == "_") has_wildcard_in_or = true;
                }
                patterns.push_back(first_pat);
            }

            // Parse additional or-pattern alternatives: | pat2 | pat3 ...
            while (match({TokenType::PIPE})) {
                std::shared_ptr<Expr> alt_pat = parseMatchPattern();
                std::vector<Token> alt_vars;
                collectBindings(alt_pat, alt_vars);

                // All alternatives must bind the same variables
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
                } else if (!alt_vars.empty() || !variables.empty()) {
                    error(peek(),
                        "Or-pattern alternatives must bind the same number of variables. "
                        "Expected " + std::to_string(variables.size()) + " but got " +
                        std::to_string(alt_vars.size()) + ".",
                        "E400");
                }

                alt_variables.push_back(std::move(alt_vars));

                if (auto ve = std::dynamic_pointer_cast<const VarExpr>(alt_pat)) {
                    if (ve->name.lexeme == "_") has_wildcard_in_or = true;
                }
                patterns.push_back(alt_pat);
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
