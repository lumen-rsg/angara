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

            consume(TokenType::FROM, "Expected 'from' after symbol names in selective attach.", "E170");

            Token modulePath;
            if (match({TokenType::STRING})) {
                modulePath = previous();
            } else if (match({TokenType::IDENTIFIER})) {
                modulePath = previous();
            } else {
                throw error(peek(), "Expected a module path (string literal) or module name (identifier) after 'from'.", "E171");
            }

            consume(TokenType::SEMICOLON, "Expected ';' after 'attach' statement.", "E172");
            return std::make_shared<AttachStmt>(std::move(names), std::move(modulePath), std::nullopt);

        } else {
            Token modulePath;
            if (check(TokenType::IDENTIFIER) || check(TokenType::STRING)) {
                modulePath = advance();
            } else {
                throw error(peek(), "Expected a module name or path after 'attach'.", "E173");
            }

            std::optional<Token> alias;
            if (match({TokenType::AS})) {
                alias = consume(TokenType::IDENTIFIER, "Expected alias name after 'as'.", "E174");
            }

            consume(TokenType::SEMICOLON, "Expected ';' after 'attach' statement.", "E175");
            return std::make_shared<AttachStmt>(std::vector<Token>{}, modulePath, alias);
        }
    }

}
