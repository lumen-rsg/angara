#include "Parser.h"
namespace angara {
    std::shared_ptr<Stmt> Parser::parseForInLoop(const Token& keyword) {
        // LANG-10: destructuring for-in — `for (k, v) in expr { ... }`
        if (match({TokenType::LEFT_PAREN})) {
            std::vector<Token> names;
            if (!check(TokenType::RIGHT_PAREN)) {
                do {
                    Token var_name = consume(TokenType::IDENTIFIER, "Expected variable name in for-in destructuring pattern.", "E179");
                    names.push_back(var_name);
                } while (match({TokenType::COMMA}));
            }
            consume(TokenType::RIGHT_PAREN, "Expected ')' after destructuring pattern.", "E213");

            if (names.size() < 2) {
                throw error(previous(), "Destructuring for-in requires at least two variable names. Use a single name without parentheses for single-variable iteration.", "E214");
            }

            consume(TokenType::IN, "Expected 'in' after iteration variables in 'for-in' loop.", "E180");
            std::shared_ptr<Expr> collection = expression();
            consume(TokenType::RIGHT_PAREN, "Expected ')' after 'for-in' clauses.", "E181");
            consume(TokenType::LEFT_BRACE, "Expected '{' to begin 'for-in' loop body.", "E182");
            std::shared_ptr<Stmt> body = std::make_shared<BlockStmt>(block());

            return std::make_shared<ForInStmt>(keyword, std::move(names), std::move(collection), std::move(body));
        }

        Token name = consume(TokenType::IDENTIFIER, "Expected iteration variable name in 'for-in' loop.", "E179");
        consume(TokenType::IN, "Expected 'in' after iteration variable in 'for-in' loop.", "E180");
        std::shared_ptr<Expr> collection = expression();
        consume(TokenType::RIGHT_PAREN, "Expected ')' after 'for-in' clauses.", "E181");
        consume(TokenType::LEFT_BRACE, "Expected '{' to begin 'for-in' loop body.", "E182");
        std::shared_ptr<Stmt> body = std::make_shared<BlockStmt>(block());

        return std::make_shared<ForInStmt>(keyword, std::move(name), std::move(collection), std::move(body));
    }

    bool Parser::isForInLoop() {
        int current = m_current;
        // LANG-10: track parenthesis depth so nested ((k,v)) patterns don't
        // fool the scanner into stopping at the inner closing paren.
        int paren_depth = 0;
        while (current < static_cast<int>(m_tokens.size()) &&
               m_tokens[current].type != TokenType::EOF_TOKEN) {

            if (m_tokens[current].type == TokenType::LEFT_PAREN) {
                paren_depth++;
            } else if (m_tokens[current].type == TokenType::RIGHT_PAREN) {
                if (paren_depth == 0) {
                    // Closing paren at depth 0: this is the for-loop's closing paren.
                    // We've exhausted the loop header without seeing IN or SEMICOLON.
                    return false;
                }
                paren_depth--;
            }

            if (m_tokens[current].type == TokenType::IN) {
                return true;
            }
            if (m_tokens[current].type == TokenType::SEMICOLON && paren_depth == 0) {
                return false;
            }
            current++;
        }
        return false;
    }

    std::shared_ptr<Stmt> Parser::parseCStyleLoop(const Token& keyword) {
        std::shared_ptr<Stmt> initializer;
        if (match({TokenType::SEMICOLON})) {
            initializer = nullptr;
        } else if (match({TokenType::LET})) {
            initializer = varDeclaration(false);
        } else {
            initializer = expressionStatement();
        }

        std::shared_ptr<Expr> condition = nullptr;
        if (!check(TokenType::SEMICOLON)) {
            condition = expression();
        }
        consume(TokenType::SEMICOLON, "Expected ';' after loop condition.", "E183");

        std::shared_ptr<Expr> increment = nullptr;
        if (!check(TokenType::RIGHT_PAREN)) {
            increment = expression();
        }
        consume(TokenType::RIGHT_PAREN, "Expected ')' after 'for' clauses.", "E184");

        consume(TokenType::LEFT_BRACE, "Expected '{' to begin 'for' loop body.", "E185");
        std::shared_ptr<Stmt> body = std::make_shared<BlockStmt>(block());

        return std::make_shared<ForStmt>(keyword, std::move(initializer), std::move(condition), std::move(increment), std::move(body));
    }

    std::shared_ptr<Stmt> Parser::forStatement() {
        Token keyword = previous();

        consume(TokenType::LEFT_PAREN, "Expected '(' after 'for'.", "E186");

        if (isForInLoop()) {
            return parseForInLoop(keyword);
        } else {
            return parseCStyleLoop(keyword);
        }
    }

}
