#include "Parser.h"

namespace angara {
    //
    // Grammar:
    //   asmExpr := "asm" "(" STRING ("->" type)? ("," operand)* ")"
    //   operand := dir STRING expr
    //   dir     := "in" | "out" | "inout"
    //
    // The optional `-> type` clause comes right after the template string and
    // gives the asm's result type (defaults to nil). Placing it first avoids any
    // ambiguity with operand expressions: e.g.
    //     asm("mrs $0, CurrentEL" -> i64, out("=r") el)
    // `in` is a reserved keyword (TokenType::IN); `out` and `inout` are plain
    // identifiers matched contextually here. The constraint and template are
    // both STRING literals whose lexemes are already unescaped by the lexer.
    //
    std::shared_ptr<Expr> Parser::asmExpression() {
        Token keyword = previous();  // the 'asm' token

        consume(TokenType::LEFT_PAREN, "Expected '(' after 'asm'.", "E250");

        Token asm_string = consume(TokenType::STRING,
            "Expected a string literal with the assembly template inside 'asm(...)'.", "E251");

        std::shared_ptr<ASTType> result_type = nullptr;
        if (match({TokenType::MINUS_GREATER})) {
            result_type = type();
        }

        std::vector<AsmOperand> operands;
        while (match({TokenType::COMMA})) {
            AsmDir dir;
            // 'in' arrives as the IN keyword token; 'out'/'inout' as identifiers.
            if (match({TokenType::IN})) {
                dir = AsmDir::IN;
            } else if (check(TokenType::IDENTIFIER)) {
                const std::string& lex = peek().lexeme;
                if (lex == "out") {
                    dir = AsmDir::OUT;
                } else if (lex == "inout") {
                    dir = AsmDir::INOUT;
                } else {
                    throw error(peek(), "Expected 'in', 'out', or 'inout' as an asm operand direction.", "E252");
                }
                advance();
            } else {
                throw error(peek(), "Expected 'in', 'out', or 'inout' as an asm operand direction.", "E252");
            }

            consume(TokenType::LEFT_PAREN, "Expected '(' before the operand constraint string.", "E253");
            Token constraint = consume(TokenType::STRING,
                "Expected a constraint string after the operand direction.", "E254");
            consume(TokenType::RIGHT_PAREN, "Expected ')' after the operand constraint string.", "E255");

            auto operand_expr = expression();
            operands.push_back(AsmOperand{dir, std::move(constraint), std::move(operand_expr)});
        }

        consume(TokenType::RIGHT_PAREN, "Expected ')' to close the 'asm(...)' expression.", "E256");

        return std::make_shared<AsmExpr>(std::move(keyword), std::move(asm_string),
                                         std::move(operands), std::move(result_type));
    }
}
