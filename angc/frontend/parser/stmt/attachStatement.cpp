//
// Created by cv2 on 9/19/25.
//
#include "Parser.h"
namespace angara {

    bool Parser::isSelectiveAttach() {
        int current = m_current;
        // Look for a 'FROM' keyword before we find a semicolon.
        while (m_tokens[current].type != TokenType::SEMICOLON &&
               m_tokens[current].type != TokenType::EOF_TOKEN) {
            if (m_tokens[current].type == TokenType::FROM) {
                return true; // Found 'from', so it's the selective form.
            }
            current++;
               }
        return false; // No 'from' found, it's the simple form.
    }

    std::shared_ptr<Stmt> Parser::attachStatement() {
        // Look ahead to see what kind of attach statement this is.
        if (isSelectiveAttach()) {
            // --- Parse the Selective Form: attach name1, name2 from "path" or module_name ---
            std::vector<Token> names;
            do {
                names.push_back(consume(TokenType::IDENTIFIER, "Expect name to attach."));
            } while (match({TokenType::COMMA}));

            consume(TokenType::FROM, "Expect 'from' after attached names.");

            Token modulePath;
            if (match({TokenType::STRING})) {
                modulePath = previous();
            } else if (match({TokenType::IDENTIFIER})) {
                modulePath = previous();
            } else {
                throw error(peek(), "Expect module path (string literal) or module name (identifier) after 'from'.");
            }

            consume(TokenType::SEMICOLON, "Expect ';' after attach statement.");
            // The 'alias' doesn't make sense in a selective import. We pass std::nullopt.
            return std::make_shared<AttachStmt>(std::move(names), std::move(modulePath), std::nullopt);

        } else {
            // --- Parse the Simple Form: attach "path" or attach name ---
            Token modulePath;
            if (check(TokenType::IDENTIFIER) || check(TokenType::STRING)) {
                modulePath = advance();
            } else {
                throw error(peek(), "Expect module name or path after 'attach'.");
            }

            std::optional<Token> alias;
            if (match({TokenType::AS})) {
                alias = consume(TokenType::IDENTIFIER, "Expect alias name after 'as'.");
            }

            consume(TokenType::SEMICOLON, "Expect ';' after attach statement.");
            return std::make_shared<AttachStmt>(std::vector<Token>{}, modulePath, alias);
        }
    }

}