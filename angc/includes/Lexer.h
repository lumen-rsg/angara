#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include "Token.h"
#include "ErrorHandler.h"
namespace angara {

    class Lexer {
    public:
        // Constructor takes the source code to be scanned
        Lexer(std::string source, std::shared_ptr<std::string> filename, ErrorHandler& errorHandler);

        // The main function that scans all tokens and returns them as a vector
        std::vector<Token> scanTokens();

    private:
        // Helper methods for the scanning process
        bool isAtEnd() const;
        void scanToken();
        char advance();
        bool match(char expected);
        char peek();
        char peekNext() const;
        char peekAhead(int offset) const;
        void string();
        void number();
        void identifier();
        void addToken(TokenType type);
        void addToken(TokenType type, const std::string &literal);
        void blockComment();

        const std::string m_source;
        std::vector<Token> m_tokens;
        int m_start = 0;
        int m_current = 0;
        int m_line = 1;
        int m_column = 1;
        std::shared_ptr<std::string> m_filename;
        ErrorHandler& m_errorHandler;

        // Map to hold all reserved keywords (O(1) lookup)
        static const std::unordered_map<std::string, TokenType> keywords;

        void multilineString();
    };
}