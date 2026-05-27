#include "Parser.h"
namespace angara {

    bool Parser::isSelectiveAttach() {
        int current = m_current;
        while (current < static_cast<int>(m_tokens.size()) &&
               m_tokens[current].type != TokenType::SEMICOLON &&
               m_tokens[current].type != TokenType::EOF_TOKEN) {
            if (m_tokens[current].type == TokenType::FROM) {
                return true;
            }
            current++;
        }
        return false;
    }

    std::shared_ptr<Stmt> Parser::attachStatement() {
        if (isSelectiveAttach()) {
            std::vector<Token> names;
            do {
                names.push_back(consume(TokenType::IDENTIFIER, "Expected symbol name to attach."));
            } while (match({TokenType::COMMA}));

            consume(TokenType::FROM, "Expected 'from' after symbol names in selective attach.");

            Token modulePath;
            if (match({TokenType::STRING})) {
                modulePath = previous();
            } else if (match({TokenType::IDENTIFIER})) {
                modulePath = previous();
            } else {
                throw error(peek(), "Expected a module path (string literal) or module name (identifier) after 'from'.");
            }

            consume(TokenType::SEMICOLON, "Expected ';' after 'attach' statement.");
            return std::make_shared<AttachStmt>(std::move(names), std::move(modulePath), std::nullopt);

        } else {
            Token modulePath;
            if (check(TokenType::IDENTIFIER) || check(TokenType::STRING)) {
                modulePath = advance();
            } else {
                throw error(peek(), "Expected a module name or path after 'attach'.");
            }

            std::optional<Token> alias;
            if (match({TokenType::AS})) {
                alias = consume(TokenType::IDENTIFIER, "Expected alias name after 'as'.");
            }

            consume(TokenType::SEMICOLON, "Expected ';' after 'attach' statement.");
            return std::make_shared<AttachStmt>(std::vector<Token>{}, modulePath, alias);
        }
    }

}
