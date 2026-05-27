#include "Lexer.h"
#include <sstream>
#include <utility>

namespace angara {

    const std::unordered_map<std::string, TokenType> Lexer::keywords = {
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
            {"contract", TokenType::CONTRACT},
            {"signs",    TokenType::SIGNS},
            {"private",  TokenType::PRIVATE},
            {"public",   TokenType::PUBLIC},
            {"break",    TokenType::BREAK},
            {"continue", TokenType::CONTINUE},
            {"is",       TokenType::IS},
            {"data",     TokenType::DATA},
            {"enum",     TokenType::ENUM},
            {"match",    TokenType::MATCH},
            {"case",     TokenType::CASE},
            {"foreign",   TokenType::FOREIGN},
            {"function",  TokenType::TYPE_FUNCTION},
            {"intrinsic", TokenType::INTRINSIC},
            {"sizeof",   TokenType::SIZEOF},
            {"retype",   TokenType::RETYPE},
    };

    Lexer::Lexer(std::string source, std::shared_ptr<std::string> filename, ErrorHandler& errorHandler)
            : m_source(std::move(source)), m_filename(std::move(filename)), m_errorHandler(errorHandler) {}

    std::vector<Token> Lexer::scanTokens() {
        while (!isAtEnd()) {
            m_start = m_current;
            scanToken();
        }

        m_tokens.emplace_back(TokenType::EOF_TOKEN, "", m_line, 1, m_filename);
        return m_tokens;
    }

    bool isDigit(char c) {
        return c >= '0' && c <= '9';
    }

    bool isHexDigit(char c) {
        return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }

    bool isBinaryDigit(char c) {
        return c == '0' || c == '1';
    }

    bool isAlpha(char c) {
        return (c >= 'a' && c <= 'z') ||
               (c >= 'A' && c <= 'Z') ||
               c == '_';
    }

    bool isAlphaNumeric(char c) {
        return isAlpha(c) || isDigit(c);
    }

    bool Lexer::isAtEnd() const {
        return m_current >= m_source.length();
    }

    char Lexer::advance() {
        m_column++;
        return m_source[m_current++];
    }

    void Lexer::addToken(TokenType type) {
        std::string text = m_source.substr(m_start, m_current - m_start);
        int token_col = m_column - static_cast<int>(text.length());
        m_tokens.emplace_back(type, std::move(text), m_line, token_col, m_filename);
    }

    void Lexer::addToken(TokenType type, const std::string &literal) {
        int token_col = m_column - (m_current - m_start);
        m_tokens.emplace_back(type, literal, m_line, token_col, m_filename);
    }

    bool Lexer::match(char expected) {
        if (isAtEnd()) return false;
        if (m_source[m_current] != expected) return false;

        m_current++;
        m_column++;
        return true;
    }

    char Lexer::peek() const {
        if (isAtEnd()) return '\0';
        return m_source[m_current];
    }

    char Lexer::peekNext() const {
        if (m_current + 1 >= m_source.length()) return '\0';
        return m_source[m_current + 1];
    }

    char Lexer::peekAhead(int offset) const {
        if (m_current + offset >= m_source.length()) return '\0';
        return m_source[m_current + offset];
    }

    void Lexer::blockComment() {
        int depth = 1;

        while (depth > 0 && !isAtEnd()) {
            if (peek() == '/' && peekNext() == '*') {
                advance();
                advance();
                depth++;
            } else if (peek() == '*' && peekNext() == '/') {
                advance();
                advance();
                depth--;
            } else {
                if (peek() == '\n') {
                    m_line++;
                    m_column = 0;
                }
                advance();
            }
        }

        if (depth > 0) {
            m_errorHandler.report(
                Token(TokenType::EOF_TOKEN, "*/", m_line, m_column, m_filename),
                "Unterminated block comment."
            );
        }
    }

    void Lexer::string() {
        std::stringstream value;

        while (peek() != '"' && !isAtEnd()) {
            if (peek() == '\n') {
                m_errorHandler.report(
                    Token(TokenType::STRING, m_source.substr(m_start, m_current - m_start), m_line, m_column, m_filename),
                    "Unterminated string literal."
                );
                return;
            }

            char c = advance();

            if (c == '\\') {
                if (isAtEnd()) {
                    m_errorHandler.report(
                        Token(TokenType::STRING, "", m_line, m_column, m_filename),
                        "Unterminated string literal; ends with '\\'."
                    );
                    return;
                }

                char escaped = advance();
                switch (escaped) {
                    case '"':  value << '"'; break;
                    case '\\': value << '\\'; break;
                    case 'n':  value << '\n'; break;
                    case 'r':  value << '\r'; break;
                    case 't':  value << '\t'; break;
                    case 'b':  value << '\b'; break;
                    case 'f':  value << '\f'; break;
                    case 'v':  value << '\v'; break;
                    case 'a':  value << '\a'; break;

                    case '0': case '1': case '2': case '3':
                    case '4': case '5': case '6': case '7': {
                        std::string octal_str;
                        octal_str += escaped;
                        for (int i = 0; i < 2; ++i) {
                            if (peek() >= '0' && peek() <= '7') {
                                octal_str += advance();
                            } else {
                                break;
                            }
                        }
                        char octal_char = static_cast<char>(strtol(octal_str.c_str(), nullptr, 8));
                        value << octal_char;
                        break;
                    }

                    case 'x': {
                        std::string hex_str;
                        for (int i = 0; i < 2; ++i) {
                            if (isxdigit(peek())) {
                                hex_str += advance();
                            } else {
                                break;
                            }
                        }
                        if (hex_str.empty()) {
                            m_errorHandler.report(
                                Token(TokenType::STRING, "", m_line, m_column, m_filename),
                                "Incomplete hex escape sequence '\\x'."
                            );
                        } else {
                            char hex_char = static_cast<char>(strtol(hex_str.c_str(), nullptr, 16));
                            value << hex_char;
                        }
                        break;
                    }

                    case 'u':
                    case 'U': {
                        m_errorHandler.report(
                            Token(TokenType::STRING, "", m_line, m_column, m_filename),
                            "Unicode escape sequences ('\\u', '\\U') are not yet supported."
                        );
                        int limit = (escaped == 'u' ? 4 : 8);
                        for (int i = 0; i < limit; ++i) { if (isxdigit(peek())) advance(); }
                        break;
                    }

                    default:
                        m_errorHandler.report(
                            Token(TokenType::STRING, "", m_line, m_column, m_filename),
                            "Unknown escape sequence '\\" + std::string(1, escaped) + "'."
                        );
                        value << escaped;
                        break;
                }
            } else {
                value << c;
            }
        }

        if (isAtEnd()) {
            m_errorHandler.report(
                Token(TokenType::STRING, m_source.substr(m_start, m_current - m_start), m_line, m_column, m_filename),
                "Unterminated string literal."
            );
            return;
        }

        advance();

        addToken(TokenType::STRING, value.str());
    }

    void Lexer::multilineString() {
        while (!(peek() == '"' && peekNext() == '"' && peekAhead(2) == '"') && !isAtEnd()) {
            if (peek() == '\n') {
                m_line++;
                m_column = 0;
            }
            advance();
        }

        if (isAtEnd()) {
            m_errorHandler.report(
                Token(TokenType::STRING, "\"\"\"", m_line, m_column, m_filename),
                "Unterminated multi-line string."
            );
            return;
        }

        advance();
        advance();
        advance();

        std::string value = m_source.substr(m_start + 3, m_current - m_start - 6);
        addToken(TokenType::STRING, value);
    }

    void Lexer::number() {
        if (m_source[m_start] == '0') {
            char next = peek();
            if (next == 'x' || next == 'X') {
                advance();

                if (!isHexDigit(peek())) {
                    m_errorHandler.report(
                        Token(TokenType::NUMBER_INT, "0x", m_line, m_column - 2, m_filename),
                        "Expected hexadecimal digits after '0x'."
                    );
                    return;
                }

                while (isHexDigit(peek()) || peek() == '_') {
                    if (peek() == '_') {
                        advance();
                        if (!isHexDigit(peek())) {
                            m_errorHandler.report(
                                Token(TokenType::NUMBER_INT, "_", m_line, m_column - 1, m_filename),
                                "Numeric separator '_' must be followed by a digit."
                            );
                            return;
                        }
                        continue;
                    }
                    advance();
                }

                addToken(TokenType::NUMBER_INT);
                return;
            }

            if (next == 'b' || next == 'B') {
                advance();

                if (!isBinaryDigit(peek())) {
                    m_errorHandler.report(
                        Token(TokenType::NUMBER_INT, "0b", m_line, m_column - 2, m_filename),
                        "Expected binary digits (0 or 1) after '0b'."
                    );
                    return;
                }

                while (isBinaryDigit(peek()) || peek() == '_') {
                    if (peek() == '_') {
                        advance();
                        if (!isBinaryDigit(peek())) {
                            m_errorHandler.report(
                                Token(TokenType::NUMBER_INT, "_", m_line, m_column - 1, m_filename),
                                "Numeric separator '_' must be followed by a digit."
                            );
                            return;
                        }
                        continue;
                    }
                    advance();
                }

                addToken(TokenType::NUMBER_INT);
                return;
            }
        }

        while (isDigit(peek()) || peek() == '_') {
            if (peek() == '_') {
                advance();
                if (!isDigit(peek())) {
                    m_errorHandler.report(
                        Token(TokenType::NUMBER_INT, "_", m_line, m_column - 1, m_filename),
                        "Numeric separator '_' must be followed by a digit."
                    );
                    return;
                }
                continue;
            }
            advance();
        }

        if (peek() == '.' && isDigit(peekNext())) {
            advance();
            while (isDigit(peek()) || peek() == '_') {
                if (peek() == '_') {
                    advance();
                    if (!isDigit(peek())) {
                        m_errorHandler.report(
                            Token(TokenType::NUMBER_FLOAT, "_", m_line, m_column - 1, m_filename),
                            "Numeric separator '_' must be followed by a digit."
                        );
                        return;
                    }
                    continue;
                }
                advance();
            }
            addToken(TokenType::NUMBER_FLOAT);
        } else {
            addToken(TokenType::NUMBER_INT);
        }
    }

    void Lexer::identifier() {
        while (isAlphaNumeric(peek())) advance();

        std::string text = m_source.substr(m_start, m_current - m_start);

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
                        addToken(TokenType::DOT_DOT);
                    }
                } else {
                    addToken(TokenType::DOT);
                }
                break;
            case '*':
                addToken(match('=') ? TokenType::STAR_EQUAL : TokenType::STAR);
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
            case '@':
                addToken(TokenType::AT_SIGN);
                break;

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
                addToken(match('&') ? TokenType::LOGICAL_AND : TokenType::AMPERSAND);
                break;
            case '^':
                addToken(TokenType::CARET);
                break;
            case '~':
                addToken(TokenType::TILDE);
                break;
            case '?':
                if (match('?')) {
                    addToken(TokenType::QUESTION_QUESTION);
                } else if (match('.')) {
                    addToken(TokenType::QUESTION_DOT);
                } else {
                    addToken(TokenType::QUESTION);
                }
                break;

            case '/':
                if (match('=')) {
                    addToken(TokenType::SLASH_EQUAL);
                } else if (match('/')) {
                    while (peek() != '\n' && !isAtEnd()) advance();
                } else if (match('*')) {
                    blockComment();
                } else {
                    addToken(TokenType::SLASH);
                }
                break;

            case '"':
                if (peek() == '"' && peekNext() == '"') {
                    advance();
                    advance();
                    multilineString();
                } else {
                    string();
                }
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
                if (isDigit(c)) {
                    number();
                } else if (isAlpha(c)) {
                    identifier();
                } else {
                    m_errorHandler.report(
                        Token(TokenType::IDENTIFIER, std::string(1, c), m_line, m_column - 1, m_filename),
                        "Unexpected character '" + std::string(1, c) + "'."
                    );
                }
                break;
        }
    }

}
