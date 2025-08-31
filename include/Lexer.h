#pragma once

#include <string>
#include <vector>
#include <map>
#include "Token.h" // Adjust path to find Token.h
namespace angara {

    class Lexer {
    public:
        // Constructor takes the source code to be scanned
        Lexer(const std::string &source);

        // The main function that scans all tokens and returns them as a vector
        std::vector<Token> scanTokens();

    private:
        // Helper methods for the scanning process
        bool isAtEnd();
        void scanToken();
        char advance();
        bool match(char expected);
        char peek();
        char peekNext();
        void string();
        void number();
        void identifier();
        void addToken(TokenType type);
        void addToken(TokenType type, const std::string &literal);

        const std::string m_source;
        std::vector<Token> m_tokens;
        int m_start = 0;
        int m_current = 0;
        int m_line = 1;
        int m_column = 1;
        bool m_isAtStartOfLine = true;

        // Map to hold all reserved keywords
        static const std::map<std::string, TokenType> keywords;
    };
}