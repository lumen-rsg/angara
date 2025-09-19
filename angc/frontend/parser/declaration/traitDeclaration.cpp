//
// Created by cv2 on 9/19/25.
//

#include "Parser.h"
namespace angara {

    std::shared_ptr<Stmt> Parser::traitDeclaration() {
        Token name = consume(TokenType::IDENTIFIER, "Expect trait name.");
        consume(TokenType::LEFT_BRACE, "Expect '{' before trait body.");

        std::vector<std::shared_ptr<FuncStmt>> methods;
        while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
            // A trait body can ONLY contain 'func' declarations.
            if (match({TokenType::FUNC})) {
                methods.push_back(std::static_pointer_cast<FuncStmt>(function("method")));
            } else {
                // If the user tries to write 'let', it's a syntax error.
                error(peek(), "Trait body can only contain 'func' (method) declarations.");
                if (m_panicMode) break;
                synchronize();
            }
        }

        consume(TokenType::RIGHT_BRACE, "Expect '}' after trait body.");
        return std::make_shared<TraitStmt>(std::move(name), std::move(methods));
    }

}