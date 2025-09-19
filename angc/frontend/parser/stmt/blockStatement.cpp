//
// Created by cv2 on 9/19/25.
//
#include "Parser.h"
namespace angara {

    std::vector<std::shared_ptr<Stmt>> Parser::block() {
        std::vector<std::shared_ptr<Stmt>> statements;

        while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
            statements.push_back(declaration());
        }

        consume(TokenType::RIGHT_BRACE, "Expect '}' after block.");
        return statements;
    }

}