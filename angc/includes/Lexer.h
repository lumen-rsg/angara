#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include "Token.h"
#include "ErrorHandler.h"

namespace angara {

    /// Lexical analyzer (scanner) that converts raw source text into a flat Token stream.
    class Lexer {
    public:
        /// Constructs a Lexer for the given source file.
        /// @param source      The full source code to scan.
        /// @param filename    Shared pointer to the source file path (attached to every emitted Token).
        /// @param errorHandler  Error reporter used for diagnostics.
        Lexer(std::string source, std::shared_ptr<std::string> filename, ErrorHandler& errorHandler);

        /// Scans the entire source and returns the complete list of tokens, ending with an EOF token.
        /// @return Ordered vector of all tokens in the source.
        std::vector<Token> scanTokens();

    private:
        /// Returns true if the scanner has consumed every character in the source.
        bool isAtEnd() const;

        /// Reads the next character, determines its token type, and emits it.
        void scanToken();

        /// Consumes and returns the current character, advancing the cursor by one.
        /// @return The character that was at the current position.
        char advance();

        /// Conditionally consumes the current character only if it matches @p expected.
        /// @param expected  The character to match against.
        /// @return True if the character matched and was consumed; false otherwise.
        bool match(char expected);

        /// Returns the current character without consuming it, or '\\0' if at end of source.
        /// @return The character at the current position.
        char peek();

        /// Returns the character one position ahead without consuming it, or '\\0' if out of bounds.
        /// @return The character at current+1.
        char peekNext() const;

        /// Returns the character at an arbitrary offset from current without consuming it.
        /// @param offset  Number of characters ahead to look.
        /// @return The character at current+offset, or '\\0' if out of bounds.
        char peekAhead(int offset) const;

        /// Scans a double-quoted string literal, handling escape sequences, and emits a STRING token.
        void string();

        /// Scans a triple-quoted (""") multiline string literal and emits a STRING token.
        void multilineString();

        /// Scans a numeric literal (decimal, hex 0x, binary 0b) with optional underscore separators,
        /// emitting either NUMBER_INT or NUMBER_FLOAT.
        void number();

        /// Scans an identifier or keyword and emits the appropriate token type.
        void identifier();

        /// Emits a token whose lexeme is the current lexeme (m_start..m_current).
        /// @param type  The token type to emit.
        void addToken(TokenType type);

        /// Emits a token with an explicit literal value that may differ from the raw lexeme.
        /// @param type     The token type to emit.
        /// @param literal  The resolved literal value (e.g., string contents with escapes processed).
        void addToken(TokenType type, const std::string &literal);

        /// Scans a nested block comment (/* ... */) and discards it. Reports an error if unterminated.
        void blockComment();

        const std::string m_source;
        std::vector<Token> m_tokens;
        int m_start = 0;
        int m_current = 0;
        int m_line = 1;
        int m_column = 1;
        std::shared_ptr<std::string> m_filename;
        ErrorHandler& m_errorHandler;

        /// Static keyword-to-TokenType map for O(1) reserved-word lookup.
        static const std::unordered_map<std::string, TokenType> keywords;
    };
}
