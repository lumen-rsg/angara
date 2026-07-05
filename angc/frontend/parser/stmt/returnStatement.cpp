#include "Parser.h"
namespace angara {

    std::shared_ptr<Stmt> Parser::returnStatement() {
        Token keyword = previous();
        std::shared_ptr<Expr> value = nullptr;
        if (!check(TokenType::SEMICOLON)) {
            // LANG-10: multi-return -- parse comma-separated values
            std::vector<std::shared_ptr<Expr>> values;
            values.push_back(expression());
            while (match({TokenType::COMMA})) {
                values.push_back(expression());
            }
            if (values.size() == 1) {
                value = std::move(values[0]);
            } else {
                // Synthesise a TupleExpr so that `return a, b` is equivalent to
                // `return (a, b)` -- type checking and codegen already handle tuples.
                value = std::make_shared<TupleExpr>(keyword, std::move(values));
            }
        }
        consume(TokenType::SEMICOLON, "Expected ';' after 'return' value.", "E199");
        return std::make_shared<ReturnStmt>(std::move(keyword), std::move(value));
    }

}
