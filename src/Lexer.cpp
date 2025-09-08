//
// Created by cv2 on 8/27/25.
//

#include "../include/Lexer.h"
#include <iostream> // For error reporting

namespace angara {
// Initialize the static keywords map
    const std::map<std::string, TokenType> Lexer::keywords = {
            {"let",      TokenType::LET},
            {"const",    TokenType::CONST},
            {"if",       TokenType::IF},
            {"orif",     TokenType::ORIF},
            {"else",     TokenType::ELSE},
            {"for",      TokenType::FOR},
            {"while",    TokenType::WHILE},
            {"in",       TokenType::IN},
            {"func",     TokenType::FUNC},
            {"return",   TokenType::RETURN},
            {"true",     TokenType::TRUE},
            {"false",    TokenType::FALSE},
            {"try",      TokenType::TRY},
            {"catch",    TokenType::CATCH},
            {"attach",   TokenType::ATTACH},
            {"string",   TokenType::TYPE_STRING},
            {"int",      TokenType::TYPE_INT},
            {"float",    TokenType::TYPE_FLOAT},
            {"bool",     TokenType::TYPE_BOOL},
            {"list",     TokenType::TYPE_LIST},
            {"map",      TokenType::TYPE_MAP},
            {"nil",      TokenType::NIL},
            {"throw",    TokenType::THROW},
            {"from",     TokenType::FROM},
            {"class",    TokenType::CLASS},
            {"this",     TokenType::THIS},
            {"inherits", TokenType::INHERITS},
            {"super",    TokenType::SUPER},
            {"trait",    TokenType::TRAIT},
            {"uses",     TokenType::USES},
            {"static",   TokenType::STATIC},
            {"export",   TokenType::EXPORT},
            {"as",       TokenType::AS},

            // Integer Types
            {"i8",       TokenType::TYPE_I8},
            {"i16",      TokenType::TYPE_I16},
            {"i32",      TokenType::TYPE_I32},
            {"i64",      TokenType::TYPE_I64},
            {"int",      TokenType::TYPE_INT}, // Alias
            {"u8",       TokenType::TYPE_U8},
            {"u16",      TokenType::TYPE_U16},
            {"u32",      TokenType::TYPE_U32},
            {"u64",      TokenType::TYPE_U64},
            {"uint",     TokenType::TYPE_UINT}, // Alias

            // Float Types
            {"f32",      TokenType::TYPE_F32},
            {"f64",      TokenType::TYPE_F64},
            {"float",    TokenType::TYPE_FLOAT}, // Alias

            // Other Primitives
            {"bool",     TokenType::TYPE_BOOL},
            {"string",   TokenType::TYPE_STRING},
            {"nil",      TokenType::TYPE_NIL},

            // Compound Types
            {"list",     TokenType::TYPE_LIST},
            {"record",   TokenType::TYPE_RECORD},
            {"function", TokenType::TYPE_FUNCTION},
            {"any",      TokenType::TYPE_ANY},
            {"private",  TokenType::PRIVATE},
            {"public",   TokenType::PUBLIC},
            {"void",     TokenType::TYPE_VOID},
            {"Thread", TokenType::TYPE_THREAD}
    };

    Lexer::Lexer(const std::string &source) : m_source(source) {}

    std::vector<Token> Lexer::scanTokens() {
        while (!isAtEnd()) {
            m_start = m_current;
            scanToken();
        }

        // Add one final "end of file" token.
        m_tokens.emplace_back(TokenType::EOF_TOKEN, "", m_line, 1);
        return m_tokens;
    }


// Helper functions
    bool isDigit(char c) {
        return c >= '0' && c <= '9';
    }

    bool isAlpha(char c) {
        return (c >= 'a' && c <= 'z') ||
               (c >= 'A' && c <= 'Z') ||
               c == '_';
    }

    bool isAlphaNumeric(char c) {
        return isAlpha(c) || isDigit(c);
    }

    bool Lexer::isAtEnd() {
        return m_current >= m_source.length();
    }

    char Lexer::advance() {
        m_column++; // Increment column on every character consumption
        return m_source[m_current++];
    }

    void Lexer::addToken(TokenType type) {
        std::string text = m_source.substr(m_start, m_current - m_start);
        // Calculate the start column of the token
        int token_col = m_column - text.length();
        m_tokens.emplace_back(type, std::move(text), m_line, token_col);
    }

    void Lexer::addToken(TokenType type, const std::string &literal) {
        // start column is current column - (total length of lexeme with quotes)
        int token_col = m_column - (m_current - m_start);
        m_tokens.emplace_back(type, literal, m_line, token_col);
    }

    bool Lexer::match(char expected) {
        if (isAtEnd()) return false;
        if (m_source[m_current] != expected) return false;

        m_current++;
        return true;
    }

    char Lexer::peek() {
        if (isAtEnd()) return '\0';
        return m_source[m_current];
    }

    void Lexer::string() {
        while (peek() != '"' && !isAtEnd()) {
            if (peek() == '\n') m_line++;
            advance();
        }

        if (isAtEnd()) {
            std::cerr << "Line " << m_line << ": Unterminated string.\n";
            return;
        }

        // The closing ".
        advance();

        // Trim the surrounding quotes to get the actual string value.
        std::string value = m_source.substr(m_start + 1, m_current - m_start - 2);
        addToken(TokenType::STRING, value);
    }

    void Lexer::number() {
        while (isDigit(peek())) advance();

        // Look for a fractional part.
        if (peek() == '.' && isDigit(peekNext())) {
            // Consume the "."
            advance();
            while (isDigit(peek())) advance();
            addToken(TokenType::NUMBER_FLOAT);
        } else {
            addToken(TokenType::NUMBER_INT);
        }
    }

    char Lexer::peekNext() {
        if (m_current + 1 >= m_source.length()) return '\0';
        return m_source[m_current + 1];
    }

    void Lexer::identifier() {
        while (isAlphaNumeric(peek())) advance();

        std::string text = m_source.substr(m_start, m_current - m_start);

        // Check if the identifier is a reserved keyword
        auto it = keywords.find(text);
        if (it == keywords.end()) {
            addToken(TokenType::IDENTIFIER);
        } else {
            addToken(it->second);
        }
    }

    void Lexer::scanToken() {
        char c = advance();
        switch (c) {
            // Single-character tokens
            case '(':
                addToken(TokenType::LEFT_PAREN);
                break;
            case ')':
                addToken(TokenType::RIGHT_PAREN);
                break;
            case '{':
                addToken(TokenType::LEFT_BRACE);
                break;
            case '}':
                addToken(TokenType::RIGHT_BRACE);
                break;
            case ',':
                addToken(TokenType::COMMA);
                break;
            case '.':
                if (match('.')) {
                    if (match('.')) {
                        addToken(TokenType::DOT_DOT_DOT);
                    } else {
                        // This could be a range operator '..' later.
                        // For now, it's an error.
                        std::cerr << "Error: Unexpected '..'\n";
                    }
                } else {
                    addToken(TokenType::DOT);
                }
            break;
            case '*':
                addToken(match('=') ? TokenType::STAR_EQUAL : TokenType::STAR); // <-- MODIFY
                break;
            case '%':
                addToken(TokenType::PERCENT);
                break;
            case ':':
                addToken(TokenType::COLON);
                break;
            case ';':
                addToken(TokenType::SEMICOLON);
                break;
            case '[':
                addToken(TokenType::LEFT_BRACKET);
                break;
            case ']':
                addToken(TokenType::RIGHT_BRACKET);
                break;

                // One or two character tokens
            case '!':
                addToken(match('=') ? TokenType::BANG_EQUAL : TokenType::BANG);
                break;
            case '=':
                addToken(match('=') ? TokenType::EQUAL_EQUAL : TokenType::EQUAL);
                break;
            case '<':
                addToken(match('=') ? TokenType::LESS_EQUAL : TokenType::LESS);
                break;
            case '>':
                addToken(match('=') ? TokenType::GREATER_EQUAL : TokenType::GREATER);
                break;
            case '+':
                if (match('+')) addToken(TokenType::PLUS_PLUS);
                else if (match('=')) addToken(TokenType::PLUS_EQUAL);
                else addToken(TokenType::PLUS);
                break;
            case '-':
                if (match('>')) addToken(TokenType::MINUS_GREATER);
                else if (match('-')) addToken(TokenType::MINUS_MINUS);
                else if (match('=')) addToken(TokenType::MINUS_EQUAL);
                else addToken(TokenType::MINUS);
                break;
            case '|':
                addToken(match('|') ? TokenType::LOGICAL_OR : TokenType::PIPE);
                break;
            case '&':
                if (match('&')) { addToken(TokenType::LOGICAL_AND); }
                else { std::cerr << "Line " << m_line << ": Unexpected character '&'\n"; }
                break;
            case '?':
                addToken(match('?') ? TokenType::QUESTION_QUESTION : TokenType::QUESTION);
                break;

                // Literals and comments
            case '/':
                if (match('=')) {
                    addToken(TokenType::SLASH_EQUAL);
                } else if (match('/')) {
                    while (peek() != '\n' && !isAtEnd()) advance();
                } else {
                    addToken(TokenType::SLASH);
                }
                break;
            case '"':
                string();
                break;

            case ' ':
            case '\r':
            case '\t':
                break;
            case '\n':
                m_line++;
                m_column = 1;
                break;

            default:
                if (isDigit(c)) { number(); }
                else if (isAlpha(c)) { identifier(); }
                else { std::cerr << "Line " << m_line << ": Unexpected character '" << c << "'\n"; }
                break;
        }
    }

}