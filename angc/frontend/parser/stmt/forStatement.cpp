#include "Parser.h"
namespace angara {
    std::shared_ptr<Stmt> Parser::parseForInLoop(const Token& keyword) {
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
        while (current < static_cast<int>(m_tokens.size()) &&
               m_tokens[current].type != TokenType::RIGHT_PAREN &&
               m_tokens[current].type != TokenType::EOF_TOKEN) {

            if (m_tokens[current].type == TokenType::IN) {
                return true;
            }
            if (m_tokens[current].type == TokenType::SEMICOLON) {
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
