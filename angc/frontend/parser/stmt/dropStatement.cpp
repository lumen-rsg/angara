#include "Parser.h"
namespace angara {
    std::shared_ptr<Stmt> Parser::dropStatement() {
        // H8: accept either a simple identifier (`drop x`) or a dotted
        // field access (`drop this.field`). Start with a primary expression
        // (identifier, `this`, etc.), then allow chained `.field` access.
        Token name = peek();  // capture first token for source location
        std::shared_ptr<Expr> target = primary();

        // Allow chained field access via `.` (but NOT function calls,
        // indexing, or other postfix operators).
        while (match({TokenType::DOT})) {
            Token op = previous();
            Token field = consume(TokenType::IDENTIFIER,
                "Expected field name after '.'.", "E218");
            target = std::make_shared<GetExpr>(std::move(target), op, std::move(field));
        }

        consume(TokenType::SEMICOLON,
            "Expected ';' after 'drop' statement.", "E502");
        return std::make_shared<DropStmt>(std::move(name), std::move(target));
    }
}
