#pragma once

#include <string>
#include <iostream>

namespace angara {

    enum class TokenType {
        // Single-character tokens
        LEFT_PAREN, RIGHT_PAREN, LEFT_BRACE, RIGHT_BRACE,
        LEFT_BRACKET, RIGHT_BRACKET,
        COMMA, DOT, MINUS, PLUS, SLASH, STAR, PERCENT,
        COLON, SEMICOLON, QUESTION,

        // Two-character operators
        PLUS_PLUS, MINUS_MINUS,
        LOGICAL_AND, LOGICAL_OR,
        PLUS_EQUAL,
        MINUS_EQUAL,
        STAR_EQUAL,
        SLASH_EQUAL,
        MINUS_GREATER,

        // One or two character tokens
        BANG, BANG_EQUAL,
        EQUAL, EQUAL_EQUAL,
        GREATER, GREATER_EQUAL,
        LESS, LESS_EQUAL,
        PIPE, QUESTION_QUESTION, DOT_DOT_DOT,

        // Literals
        IDENTIFIER, STRING, NUMBER_INT, NUMBER_FLOAT,

        // Keywords
        LET, CONST, IF, ELSE, ORIF,
        FOR, WHILE, IN,
        FUNC, RETURN, TRUE, FALSE, TRY, CATCH, ATTACH, NIL, THROW, FROM,
        CLASS, THIS, INHERITS, SUPER, TRAIT, USES, STATIC, PRIVATE, PUBLIC,

        // Type Keywords (reserved for future use)
        TYPE_STRING, TYPE_INT, TYPE_FLOAT, TYPE_BOOL,
        TYPE_LIST, TYPE_MAP, TYPE_VOID,

        // End of File
        EOF_TOKEN, AS, TYPE_I8, TYPE_I16, TYPE_I32, TYPE_I64, TYPE_U8, TYPE_U16, TYPE_U32, TYPE_U64, TYPE_UINT, TYPE_F32,
        TYPE_F64, TYPE_NIL, TYPE_RECORD, TYPE_FUNCTION, TYPE_ANY, TYPE_THREAD,
    };

// Helper to print TokenType enum values for debugging
    inline std::ostream &operator<<(std::ostream &os, const TokenType &type) {
        os << static_cast<int>(type);
        return os;
    }

    std::string to_string(const TokenType &type);

    struct Token {
        TokenType type;
        std::string lexeme;
        int line{};
        int column{};

        Token();

        Token(TokenType type, std::string lexeme, int line, int column)
                : type(type), lexeme(std::move(lexeme)), line(line), column(column) {}

        void print() const {
            std::cout << "Token(" << to_string(type)
                      << ", Lexeme: '" << lexeme
                      << "', Line: " << line
                      << ", Col: " << column << ")\n";
        }
    };

}