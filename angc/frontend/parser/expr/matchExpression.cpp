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
            std::shared_ptr<Expr> pattern = primary();
            while (match({TokenType::DOT})) {
                Token op = previous();
                Token name = consume(TokenType::IDENTIFIER, "Expected property name after '.' in match pattern.", "E223");
                pattern = std::make_shared<GetExpr>(std::move(pattern), op, std::move(name));
            }
            std::optional<Token> variable = std::nullopt;
            if (match({TokenType::LEFT_PAREN})) {
                variable = consume(TokenType::IDENTIFIER, "Expected a variable name to bind the enum variant's payload.", "E224");
                consume(TokenType::RIGHT_PAREN, "Expected ')' after pattern variable.", "E225");
            }

            consume(TokenType::COLON, "Expected ':' after match pattern.", "E226");

            std::shared_ptr<Expr> body;
            if (match({TokenType::LEFT_BRACE})) {
                body = expression();
                consume(TokenType::RIGHT_BRACE, "Expected '}' after match case body.", "E227");
            } else {
                body = expression();
            }

            cases.push_back({pattern, variable, body});
            if (!check(TokenType::RIGHT_BRACE)) {
                consume(TokenType::COMMA, "Expected ',' between match cases, or '}' to close 'match'.", "E228");
            }
        }

        consume(TokenType::RIGHT_BRACE, "Expected '}' to close 'match' body.", "E229");
        return std::make_shared<MatchExpr>(match_keyword, condition, std::move(cases));
    }

}
