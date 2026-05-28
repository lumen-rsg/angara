//
// Bitwise expression parsing — &, |, ^
//
#include "Parser.h"
namespace angara {

    // bitwise → shift ( ( "&" | "|" | "^" ) shift )*
    std::shared_ptr<Expr> Parser::bitwise() {
        std::shared_ptr<Expr> expr = shift();
        while (match({TokenType::AMPERSAND, TokenType::PIPE, TokenType::CARET})) {
            Token op = previous();
            std::shared_ptr<Expr> right = shift();
            expr = std::make_shared<Binary>(std::move(expr), std::move(op), std::move(right));
        }
        return expr;
    }

    // shift → term ( ( "<<" | ">>" ) term )*
    std::shared_ptr<Expr> Parser::shift() {
        std::shared_ptr<Expr> expr = term();
        while (match({TokenType::LSHIFT, TokenType::RSHIFT})) {
            Token op = previous();
            std::shared_ptr<Expr> right = term();
            expr = std::make_shared<Binary>(std::move(expr), std::move(op), std::move(right));
        }
        return expr;
    }

}