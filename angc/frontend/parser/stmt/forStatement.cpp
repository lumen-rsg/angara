//
// Created by cv2 on 9/19/25.
//
#include "Parser.h"
namespace angara {

    // helper for parsing the for...in variant
    std::shared_ptr<Stmt> Parser::parseForInLoop(const Token& keyword) {
        Token name = consume(TokenType::IDENTIFIER, "Expect variable name for for...in loop.");
        consume(TokenType::IN, "Expect 'in' keyword in for...in loop.");
        std::shared_ptr<Expr> collection = expression();
        consume(TokenType::RIGHT_PAREN, "Expect ')' after for..in clauses.");
        consume(TokenType::LEFT_BRACE, "Expect '{' to begin for..in loop body.");
        std::shared_ptr<Stmt> body = std::make_shared<BlockStmt>(block());

        return std::make_shared<ForInStmt>(keyword, std::move(name), std::move(collection), std::move(body));
    }

    // Helper method to look ahead for the 'in' keyword
    bool Parser::isForInLoop() {
        // We start looking from the token AFTER the opening '('
        int current = m_current;

        // A for...in loop has the simple structure: IDENTIFIER in EXPRESSION
        // A simple check is to see if an 'in' token appears before a semicolon.
        while (m_tokens[current].type != TokenType::RIGHT_PAREN &&
               m_tokens[current].type != TokenType::EOF_TOKEN) {

            if (m_tokens[current].type == TokenType::IN) {
                return true; // Found 'in', it's a for...in loop
            }
            if (m_tokens[current].type == TokenType::SEMICOLON) {
                return false; // Found ';', it's a C-style loop
            }
            current++;
               }
        return false; // Default to C-style if unsure
    }

    // helper for parsing the C-style variant
    std::shared_ptr<Stmt> Parser::parseCStyleLoop(const Token& keyword) {
        // --- 1. Initializer Clause ---
        std::shared_ptr<Stmt> initializer;
        if (match({TokenType::SEMICOLON})) {
            initializer = nullptr;
        } else if (match({TokenType::LET})) {
            initializer = varDeclaration(false);
        } else {
            initializer = expressionStatement();
        }

        // --- 2. Condition Clause ---
        std::shared_ptr<Expr> condition = nullptr;
        if (!check(TokenType::SEMICOLON)) {
            condition = expression();
        }
        consume(TokenType::SEMICOLON, "Expect ';' after loop condition.");

        // --- 3. Increment Clause ---
        std::shared_ptr<Expr> increment = nullptr;
        if (!check(TokenType::RIGHT_PAREN)) {
            increment = expression();
        }
        consume(TokenType::RIGHT_PAREN, "Expect ')' after for clauses.");

        // --- 4. Body ---
        consume(TokenType::LEFT_BRACE, "Expect '{' to begin for loop body.");
        std::shared_ptr<Stmt> body = std::make_shared<BlockStmt>(block());

        return std::make_shared<ForStmt>(keyword, std::move(initializer), std::move(condition), std::move(increment), std::move(body));
    }

    std::shared_ptr<Stmt> Parser::forStatement() {
        Token keyword = previous();

        consume(TokenType::LEFT_PAREN, "Expect '(' after 'for'.");

        if (isForInLoop()) {
            return parseForInLoop(keyword);
        } else {
            return parseCStyleLoop(keyword);
        }
    }

}