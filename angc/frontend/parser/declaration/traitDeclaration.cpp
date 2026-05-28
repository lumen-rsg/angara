#include "Parser.h"
namespace angara {

    std::shared_ptr<Stmt> Parser::traitDeclaration() {
        Token name = consume(TokenType::IDENTIFIER, "Expected trait name after 'trait'.", "E116");
        consume(TokenType::LEFT_BRACE, "Expected '{' before trait body.", "E117");

        std::vector<std::shared_ptr<FuncStmt>> methods;
        while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
            if (match({TokenType::FUNC})) {
                methods.push_back(std::static_pointer_cast<FuncStmt>(function("method")));
            } else {
                error(peek(), "Trait body can only contain method declarations ('func').", "E118");
                if (m_panicMode) break;
                synchronize();
            }
        }

        consume(TokenType::RIGHT_BRACE, "Expected '}' after trait body.", "E119");
        return std::make_shared<TraitStmt>(std::move(name), std::move(methods));
    }

}
