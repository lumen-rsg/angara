#include "Parser.h"

namespace angara {

    std::shared_ptr<Expr> Parser::call() {
        std::shared_ptr<Expr> expr = primary();

        while (true) {
            if (match({TokenType::LEFT_PAREN})) {
                std::vector<std::shared_ptr<Expr>> arguments;
                // LANG-11: per-argument names (nullopt = positional, Token = named)
                std::vector<std::optional<Token>> arg_names;
                bool seen_named = false;
                if (!check(TokenType::RIGHT_PAREN)) {
                    do {
                        if (check(TokenType::RIGHT_PAREN)) break;
                        if (arguments.size() >= 255) {
                            error(peek(), "Too many arguments in function call — maximum is 255.", "E215");
                        }
                        // LANG-11: detect named argument (IDENTIFIER COLON expression)
                        std::optional<Token> arg_name;
                        if (peek().type == TokenType::IDENTIFIER &&
                            m_current + 1 < (int)m_tokens.size() &&
                            m_tokens[m_current + 1].type == TokenType::COLON) {
                            arg_name = advance();  // consume the identifier as the name
                            advance();             // consume the COLON
                            seen_named = true;
                        } else if (seen_named) {
                            throw error(peek(), "Positional argument after named argument is not allowed. All arguments after the first named argument must use the 'name: value' syntax.", "E413");
                        }
                        arg_names.push_back(std::move(arg_name));
                        arguments.push_back(expression());
                    } while (match({TokenType::COMMA}));
                }
                Token paren = consume(TokenType::RIGHT_PAREN, "Expected ')' after function arguments.", "E216");
                expr = std::make_shared<CallExpr>(std::move(expr), std::move(paren), std::move(arguments), std::move(arg_names));

            } else if (match({TokenType::LEFT_BRACKET})) {
                Token bracket = previous();
                std::shared_ptr<Expr> index = expression();
                consume(TokenType::RIGHT_BRACKET, "Expected ']' after subscript index.", "E217");
                expr = std::make_shared<SubscriptExpr>(std::move(expr), std::move(bracket), std::move(index));

            } else if (match({TokenType::PLUS_PLUS, TokenType::MINUS_MINUS})) {
                Token op = previous();
                expr = std::make_shared<UpdateExpr>(std::move(expr), std::move(op), false /* isPrefix */);

            } else if (match({TokenType::DOT, TokenType::QUESTION_DOT})) {
                Token op = previous();
                Token name = consume(TokenType::IDENTIFIER, "Expected property or method name after '.'.", "E218");
                expr = std::make_shared<GetExpr>(std::move(expr), op, std::move(name));

            } else {
                break;
            }
        }

        return expr;
    }

}
