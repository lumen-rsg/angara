#include "Lexer.h"
#include <sstream>
#include <utility>
#include <cerrno>

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
            {"private",   TokenType::PRIVATE},
            {"protected", TokenType::PROTECTED},
            {"public",    TokenType::PUBLIC},
            {"break",    TokenType::BREAK},
            {"continue", TokenType::CONTINUE},
            {"is",       TokenType::IS},
            {"data",     TokenType::DATA},
            {"enum",     TokenType::ENUM},
            {"union",    TokenType::UNION},
            {"owned",    TokenType::OWNED},
            {"type",     TokenType::TYPE},
            {"drop",     TokenType::DROP},
            {"finally",  TokenType::FINALLY},
            {"match",    TokenType::MATCH},
            {"case",     TokenType::CASE},
            {"foreign",   TokenType::FOREIGN},
            {"function",  TokenType::TYPE_FUNCTION},
            {"intrinsic", TokenType::INTRINSIC},
            {"void",      TokenType::TYPE_VOID},
            {"async",    TokenType::ASYNC},
            {"await",    TokenType::AWAIT},
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

    bool isOctalDigit(char c) {
        return c >= '0' && c <= '7';
    }

    // L1: Unicode identifier support. Non-ASCII bytes (>= 0x80) are valid in
    // identifiers — they represent UTF-8 multi-byte sequences. This is permissive
    // (accepts any non-ASCII Unicode, including symbols), matching the behaviour
    // of Go, early Rust, and similar compilers. Full Unicode ID_Start/ID_Continue
    // classification (TR31) requires embedded property tables and is deferred.
    // L6: reject standalone UTF-8 continuation bytes (0x80-0xBF) which can never
    // start a valid sequence and indicate corrupted/malformed input.
    bool isAlpha(char c) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc >= 0x80 && uc < 0xC0) return false;  // L6: continuation byte
        return (c >= 'a' && c <= 'z') ||
               (c >= 'A' && c <= 'Z') ||
               c == '_' ||
               (uc >= 0xC0);  // UTF-8 multi-byte start
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
        if (token_col < 0) token_col = 0;
        m_tokens.emplace_back(type, std::move(text), m_line, token_col, m_filename);
    }

    void Lexer::addToken(TokenType type, const std::string &literal) {
        int token_col = m_column - (m_current - m_start);
        if (token_col < 0) token_col = 0;
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
                "Unterminated block comment.", "E001"
            );
        }
    }

    // LANG-4: shared escape-sequence decoder. The caller has already consumed
    // the backslash; `escaped` is the character that followed it. Decodes the
    // same set of escapes for both string and char literals (octal, \xNN hex;
    // \u/\U rejected with E005 — deferred to LANG-5). Appends the decoded
    // byte(s) to `out`. Reports errors via m_errorHandler. The token-type arg
    // makes the diagnostic token read sensibly in either context.
    void Lexer::lexEscape(char escaped, std::stringstream& out, TokenType diag_type,
                           uint32_t* out_cp) {
        switch (escaped) {
            case '"':  out << '"'; break;
            case '\'': out << '\''; break;
            case '\\': out << '\\'; break;
            case 'n':  out << '\n'; break;
            case 'r':  out << '\r'; break;
            case 't':  out << '\t'; break;
            case 'b':  out << '\b'; break;
            case 'f':  out << '\f'; break;
            case 'v':  out << '\v'; break;
            case 'a':  out << '\a'; break;

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
                char octal_char = 0;
                {
                    errno = 0;
                    long val = strtol(octal_str.c_str(), nullptr, 8);
                    if (errno == ERANGE) {
                        m_errorHandler.report(
                            Token(diag_type, "", m_line, m_column, m_filename),
                            "Octal escape sequence '\\" + octal_str + "' is out of range.", "E004"
                        );
                    } else if (val < 0 || val > 255) {
                        // L4: values outside 0-255 are truncated by the char cast
                        m_errorHandler.report(
                            Token(diag_type, "", m_line, m_column, m_filename),
                            "Octal escape sequence '\\" + octal_str + "' value " +
                            std::to_string(val) + " is out of range (0-255).", "E004"
                        );
                    } else {
                        // M10: val is checked to be in [0,255], but a direct
                        // static_cast<char> is implementation-defined for values
                        // with the high bit set on platforms where char is signed.
                        // Cast through unsigned char to make the conversion
                        // well-defined (modulo-256 bit pattern).
                        octal_char = static_cast<char>(static_cast<unsigned char>(val));
                    }
                }
                out << octal_char;
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
                        Token(diag_type, "", m_line, m_column, m_filename),
                        "Incomplete hex escape sequence '\\x'.", "E004"
                    );
                } else {
                    char hex_char = 0;
                    {
                        errno = 0;
                        long val = strtol(hex_str.c_str(), nullptr, 16);
                        if (errno == ERANGE) {
                            m_errorHandler.report(
                                Token(diag_type, "", m_line, m_column, m_filename),
                                "Hex escape sequence '\\x" + hex_str + "' is out of range.", "E004"
                            );
                        } else if (val < 0 || val > 255) {
                            // L4: values outside 0-255 are truncated by the char cast
                            m_errorHandler.report(
                                Token(diag_type, "", m_line, m_column, m_filename),
                                "Hex escape sequence '\\x" + hex_str + "' value " +
                                std::to_string(val) + " is out of range (0-255).", "E004"
                            );
                        } else {
                            // M10: see octal case — cast through unsigned char.
                            hex_char = static_cast<char>(static_cast<unsigned char>(val));
                        }
                    }
                    out << hex_char;
                }
                break;
            }

            case 'u':
            case 'U': {
                // LANG-5: Unicode escapes. Three forms:
                //   \uXXXX       — exactly 4 hex digits
                //   \u{XXXXXX}   — 1–6 hex digits in braces
                //   \UXXXXXXXX   — exactly 8 hex digits
                std::string hex_str;
                bool valid = true;

                if (escaped == 'u' && peek() == '{') {
                    // Braced form: \u{XXXXXX}
                    advance(); // consume '{'
                    int digits = 0;
                    while (peek() != '}' && peek() != EOF && peek() != '\n') {
                        if (isxdigit(peek())) {
                            hex_str += advance();
                            digits++;
                        } else {
                            m_errorHandler.report(
                                Token(diag_type, "", m_line, m_column, m_filename),
                                "Invalid character '" + std::string(1, peek()) +
                                    "' in Unicode escape sequence '\\u{...}'.", "E005"
                            );
                            advance(); // consume bad char for recovery
                            valid = false;
                        }
                    }
                    if (peek() == '}') {
                        advance(); // consume '}'
                    } else {
                        m_errorHandler.report(
                            Token(diag_type, "", m_line, m_column, m_filename),
                            "Unterminated Unicode escape sequence '\\u{...}' — missing '}'.", "E005"
                        );
                        valid = false;
                    }
                    if (digits == 0 && valid) {
                        m_errorHandler.report(
                            Token(diag_type, "", m_line, m_column, m_filename),
                            "Empty Unicode escape sequence '\\u{}'.", "E005"
                        );
                        valid = false;
                    }
                } else {
                    // Fixed-width form: \uXXXX (4) or \UXXXXXXXX (8)
                    int limit = (escaped == 'u' ? 4 : 8);
                    for (int i = 0; i < limit; ++i) {
                        if (isxdigit(peek())) {
                            hex_str += advance();
                        } else {
                            break;
                        }
                    }
                    if (static_cast<int>(hex_str.size()) < limit) {
                        m_errorHandler.report(
                            Token(diag_type, "", m_line, m_column, m_filename),
                            "Incomplete Unicode escape sequence '\\" + std::string(1, escaped) +
                                "'; expected " + std::to_string(limit) + " hex digits, got " +
                                std::to_string(hex_str.size()) + ".", "E005"
                        );
                        valid = false;
                    }
                }

                if (valid) {
                    unsigned long cp = 0;
                    try {
                        cp = std::stoul(hex_str, nullptr, 16);
                    } catch (const std::out_of_range&) {
                        m_errorHandler.report(
                            Token(diag_type, "", m_line, m_column, m_filename),
                            "Unicode code point U+" + hex_str + " is too large.", "E005"
                        );
                        valid = false;
                    } catch (const std::invalid_argument&) {
                        m_errorHandler.report(
                            Token(diag_type, "", m_line, m_column, m_filename),
                            "Invalid Unicode escape sequence '\\" + std::string(1, escaped) +
                                hex_str + "'.", "E005"
                        );
                        valid = false;
                    }
                    if (valid && cp > 0x10FFFF) {
                        m_errorHandler.report(
                            Token(diag_type, "", m_line, m_column, m_filename),
                            "Invalid Unicode code point U+" + hex_str +
                                " (exceeds U+10FFFF).", "E005"
                        );
                        valid = false;
                    } else if (cp >= 0xD800 && cp <= 0xDFFF) {
                        m_errorHandler.report(
                            Token(diag_type, "", m_line, m_column, m_filename),
                            "Invalid Unicode code point U+" + hex_str +
                                " (surrogate range U+D800–U+DFFF).", "E005"
                        );
                        valid = false;
                    }

                    if (valid) {
                        // UTF-8 encode the code point into `out`.
                        if (cp <= 0x7F) {
                            out << static_cast<char>(cp);
                        } else if (cp <= 0x7FF) {
                            out << static_cast<char>(0xC0 | (cp >> 6));
                            out << static_cast<char>(0x80 | (cp & 0x3F));
                        } else if (cp <= 0xFFFF) {
                            out << static_cast<char>(0xE0 | (cp >> 12));
                            out << static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            out << static_cast<char>(0x80 | (cp & 0x3F));
                        } else {
                            out << static_cast<char>(0xF0 | (cp >> 18));
                            out << static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                            out << static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            out << static_cast<char>(0x80 | (cp & 0x3F));
                        }

                        if (out_cp) *out_cp = static_cast<uint32_t>(cp);
                    }
                }
                break;
            }

            default:
                m_errorHandler.report(
                    Token(diag_type, "", m_line, m_column, m_filename),
                    "Unknown escape sequence '\\" + std::string(1, escaped) + "'.", "E006"
                );
                out << escaped;
                break;
        }
    }

    void Lexer::string() {
        std::stringstream value;

        while (peek() != '"' && !isAtEnd()) {
            if (peek() == '\n') {
                m_errorHandler.report(
                    Token(TokenType::STRING, m_source.substr(m_start, m_current - m_start), m_line, m_column, m_filename),
                    "Unterminated string literal.", "E002"
                );
                return;
            }

            char c = advance();

            if (c == '\\') {
                if (isAtEnd()) {
                    m_errorHandler.report(
                        Token(TokenType::STRING, "", m_line, m_column, m_filename),
                        "Unterminated string literal; ends with '\\'.", "E003"
                    );
                    return;
                }
                lexEscape(advance(), value, TokenType::STRING);
            } else {
                value << c;
            }
        }

        if (isAtEnd()) {
            m_errorHandler.report(
                Token(TokenType::STRING, m_source.substr(m_start, m_current - m_start), m_line, m_column, m_filename),
                "Unterminated string literal.", "E007"
            );
            return;
        }

        advance();

        addToken(TokenType::STRING, value.str());
    }

    // LANG-3: scans an interpolated string body (after the opening $" has been
    // consumed). Collects the raw source between the quotes — escape sequences
    // like \" are respected so they don't terminate the string. The parser
    // splits the raw body into literal/expr segments later.
    //
    // Brace depth is tracked so a `"` inside an expression hole (e.g. a string
    // literal or string-keyed subscript like `{m["a"]}`) does not terminate the
    // interpolated string. Inside a hole, `"` opens a nested string that runs
    // until its own closing `"` (with `\"` respected).
    void Lexer::interpolatedString() {
        std::string body;
        int brace_depth = 0;
        // A '"' only closes the interpolated string when we're not inside a
        // hole — inside a hole it opens a nested string literal (handled below).
        while ((brace_depth > 0 || peek() != '"') && !isAtEnd()) {
            if (peek() == '\n') {
                m_errorHandler.report(
                    Token(TokenType::IDENTIFIER, "", m_line, m_column, m_filename),
                    "Unterminated interpolated string literal.", "E399");
                return;
            }
            if (peek() == '\\') {
                // Keep the escape sequence verbatim — the parser will process it.
                body += advance(); // '\'
                if (!isAtEnd()) body += advance(); // the escaped char
                continue;
            }
            char c = peek();
            if (brace_depth > 0) {
                // Inside a hole: track nested braces and consume nested strings
                // verbatim so their quotes/braces don't confuse the split.
                if (c == '{') {
                    brace_depth++;
                } else if (c == '}') {
                    brace_depth--;
                } else if (c == '"') {
                    // Consume the nested string literal whole.
                    body += advance(); // opening "
                    while (peek() != '"' && !isAtEnd()) {
                        if (peek() == '\\' && !isAtEnd()) {
                            body += advance();
                            if (!isAtEnd()) body += advance();
                            continue;
                        }
                        if (peek() == '\n') break;
                        body += advance();
                    }
                    if (peek() == '"') { body += advance(); } // closing "
                    continue;
                }
                body += advance();
                continue;
            }
            // Outside any hole: '{' opens an expression hole.
            if (c == '{') {
                brace_depth++;
            }
            body += advance();
        }
        if (isAtEnd()) {
            m_errorHandler.report(
                Token(TokenType::IDENTIFIER, "", m_line, m_column, m_filename),
                "Unterminated interpolated string literal.", "E399");
            return;
        }
        advance(); // consume closing "
        addToken(TokenType::INTERP_STRING, body);
    }

    // LANG-4: scans a single-quoted char literal. Decodes the same escapes as
    // `string()` and requires exactly one resulting code point. The CHAR token's
    // lexeme is the resolved code point as a decimal string (so cgLiteral can
    // stoll it like a NUMBER_INT). Errors: E016 empty, E017 multi-char,
    // E018 unterminated. Newline inside is rejected (E018) to keep literals on
    // one source line.
    //
    // LANG-5: Unicode escapes (\u{...}, \uXXXX, \UXXXXXXXX) decode to a single
    // code point even though they produce 1–4 UTF-8 bytes in the decoded stream.
    void Lexer::charLiteral() {
        std::stringstream decoded;

        if (peek() == '\'') {
            // '' — empty. Consume the closing quote for a clean recovery.
            advance();
            m_errorHandler.report(
                Token(TokenType::CHAR, "", m_line, m_column, m_filename),
                "Empty char literal.", "E016");
            addToken(TokenType::CHAR, "0");
            return;
        }

        if (peek() == '\n' || isAtEnd()) {
            m_errorHandler.report(
                Token(TokenType::CHAR, "", m_line, m_column, m_filename),
                "Unterminated char literal.", "E018");
            return;
        }

        uint32_t unicode_cp = 0;
        bool is_unicode = false;

        char c = advance();
        if (c == '\\') {
            if (isAtEnd()) {
                m_errorHandler.report(
                    Token(TokenType::CHAR, "", m_line, m_column, m_filename),
                    "Unterminated char literal; ends with '\\'.", "E018");
                return;
            }
            lexEscape(advance(), decoded, TokenType::CHAR, &unicode_cp);
            // If lexEscape set unicode_cp (non-zero), this was a \u/\U escape.
            // Zero is a valid code point (U+0000 NUL via \u0000), so we also
            // check that the decoded output looks like UTF-8 (multi-byte).
            if (unicode_cp != 0 || decoded.str().size() > 1) {
                is_unicode = true;
            }
        } else {
            decoded << c;
        }

        // Consume to the closing quote. Any extra content before it ('ab',
        // 'a\nb') means the literal has more than one character — track that
        // so we can report E017. (A multi-byte escape like \xNN decodes to a
        // single byte and does NOT count as multi-char; likewise a \u/\U escape
        // produces 1–4 UTF-8 bytes but is still one character.)
        bool multi = false;

        while (peek() != '\'' && !isAtEnd()) {
            if (peek() == '\n') {
                m_errorHandler.report(
                    Token(TokenType::CHAR, decoded.str(), m_line, m_column, m_filename),
                    "Unterminated char literal.", "E018");
                return;
            }
            multi = true;
            advance();
        }

        if (isAtEnd()) {
            m_errorHandler.report(
                Token(TokenType::CHAR, decoded.str(), m_line, m_column, m_filename),
                "Unterminated char literal.", "E018");
            return;
        }

        advance();  // consume closing '

        std::string chars = decoded.str();

        if (multi || (!is_unicode && chars.size() != 1)) {
            m_errorHandler.report(
                Token(TokenType::CHAR, chars, m_line, m_column, m_filename),
                "Char literal must contain exactly one character.", "E017");
            addToken(TokenType::CHAR, "0");
            return;
        }

        // Code point value. For ASCII escapes (\n, \t, \x41, etc.) the single
        // decoded byte is the code point. For Unicode escapes (\u/\U) use the
        // raw code point captured by lexEscape — it may be > 0xFF.
        uint32_t cp = is_unicode ? unicode_cp
                                 : static_cast<unsigned char>(chars[0]);
        addToken(TokenType::CHAR, std::to_string(static_cast<unsigned long>(cp)));
    }

    void Lexer::multilineString() {
        std::stringstream value;

        while (!(peek() == '"' && peekNext() == '"' && peekAhead(2) == '"') && !isAtEnd()) {
            if (peek() == '\n') {
                m_line++;
                m_column = 0;
                value << advance();
                continue;
            }

            char c = advance();

            if (c == '\\') {
                if (isAtEnd()) {
                    m_errorHandler.report(
                        Token(TokenType::STRING, "\"\"\"", m_line, m_column, m_filename),
                        "Unterminated multi-line string; ends with '\\'.", "E008"
                    );
                    return;
                }
                lexEscape(advance(), value, TokenType::STRING);
            } else {
                value << c;
            }
        }

        if (isAtEnd()) {
            m_errorHandler.report(
                Token(TokenType::STRING, "\"\"\"", m_line, m_column, m_filename),
                "Unterminated multi-line string.", "E008"
            );
            return;
        }

        advance();
        advance();
        advance();

        addToken(TokenType::STRING, value.str());
    }

    // LANG-6: scans a raw string literal (r"..."). No escape processing —
    // backslashes, quotes, and every other character are consumed literally.
    // The only special character is the closing double-quote.
    // (Like Python's r"...", a raw string cannot contain an unescaped ".)
    void Lexer::rawString() {
        std::string body;

        while (peek() != '"' && !isAtEnd()) {
            if (peek() == '\n') {
                m_errorHandler.report(
                    Token(TokenType::RAW_STRING, body, m_line, m_column, m_filename),
                    "Unterminated raw string literal.", "E002"
                );
                return;
            }
            body += advance();
        }

        if (isAtEnd()) {
            m_errorHandler.report(
                Token(TokenType::RAW_STRING, body, m_line, m_column, m_filename),
                "Unterminated raw string literal.", "E008"
            );
            return;
        }

        advance();  // consume closing "
        addToken(TokenType::RAW_STRING, body);
    }

    // LANG-6: scans a byte string literal (b"..."). Escape sequences ARE
    // processed (so b"\x00" works). For now, produces a STRING-type value.
    // The b prefix is a syntactic marker — the token is BYTE_STRING, but the
    // type checker treats it identically to STRING. In the future, if a `bytes`
    // type is added, BYTE_STRING could produce it.
    void Lexer::byteString() {
        std::stringstream value;

        while (peek() != '"' && !isAtEnd()) {
            if (peek() == '\n') {
                m_errorHandler.report(
                    Token(TokenType::BYTE_STRING, "", m_line, m_column, m_filename),
                    "Unterminated byte string literal.", "E002"
                );
                return;
            }

            char c = advance();

            if (c == '\\') {
                if (isAtEnd()) {
                    m_errorHandler.report(
                        Token(TokenType::BYTE_STRING, "", m_line, m_column, m_filename),
                        "Unterminated byte string literal; ends with '\\'.", "E003"
                    );
                    return;
                }
                lexEscape(advance(), value, TokenType::BYTE_STRING);
            } else {
                value << c;
            }
        }

        if (isAtEnd()) {
            m_errorHandler.report(
                Token(TokenType::BYTE_STRING, "", m_line, m_column, m_filename),
                "Unterminated byte string literal.", "E009"
            );
            return;
        }

        advance();  // consume closing "
        addToken(TokenType::BYTE_STRING, value.str());
    }

    void Lexer::number() {
        bool is_float = false;

        if (m_source[m_start] == '0') {
            char next = peek();
            if (next == 'x' || next == 'X') {
                advance();

                if (!isHexDigit(peek())) {
                    m_errorHandler.report(
                        Token(TokenType::NUMBER_INT, "0x", m_line, m_column - 2, m_filename),
                        "Expected hexadecimal digits after '0x'.", "E009"
                    );
                    return;
                }

                while (isHexDigit(peek()) || peek() == '_') {
                    if (peek() == '_') {
                        advance();
                        if (!isHexDigit(peek())) {
                            m_errorHandler.report(
                                Token(TokenType::NUMBER_INT, "_", m_line, m_column - 1, m_filename),
                                "Numeric separator '_' must be followed by a digit.", "E010"
                            );
                            return;
                        }
                        continue;
                    }
                    advance();
                }

                goto suffix_check;
            }

            if (next == 'b' || next == 'B') {
                advance();

                if (!isBinaryDigit(peek())) {
                    m_errorHandler.report(
                        Token(TokenType::NUMBER_INT, "0b", m_line, m_column - 2, m_filename),
                        "Expected binary digits (0 or 1) after '0b'.", "E011"
                    );
                    return;
                }

                while (isBinaryDigit(peek()) || peek() == '_') {
                    if (peek() == '_') {
                        advance();
                        if (!isBinaryDigit(peek())) {
                            m_errorHandler.report(
                                Token(TokenType::NUMBER_INT, "_", m_line, m_column - 1, m_filename),
                                "Numeric separator '_' must be followed by a digit.", "E012"
                            );
                            return;
                        }
                        continue;
                    }
                    advance();
                }

                goto suffix_check;
            }

            // LANG-6: octal — 0o... / 0O...
            if (next == 'o' || next == 'O') {
                advance();

                if (!isOctalDigit(peek())) {
                    m_errorHandler.report(
                        Token(TokenType::NUMBER_INT, "0o", m_line, m_column - 2, m_filename),
                        "Expected octal digits (0-7) after '0o'.", "E019"
                    );
                    return;
                }

                while (isOctalDigit(peek()) || peek() == '_') {
                    if (peek() == '_') {
                        advance();
                        if (!isOctalDigit(peek())) {
                            m_errorHandler.report(
                                Token(TokenType::NUMBER_INT, "_", m_line, m_column - 1, m_filename),
                                "Numeric separator '_' must be followed by a digit.", "E020"
                            );
                            return;
                        }
                        continue;
                    }
                    advance();
                }

                goto suffix_check;
            }
        }

        // Decimal integer part
        while (isDigit(peek()) || peek() == '_') {
            if (peek() == '_') {
                advance();
                if (!isDigit(peek())) {
                    m_errorHandler.report(
                        Token(TokenType::NUMBER_INT, "_", m_line, m_column - 1, m_filename),
                        "Numeric separator '_' must be followed by a digit.", "E013"
                    );
                    return;
                }
                continue;
            }
            advance();
        }

        // Fractional part
        if (peek() == '.' && isDigit(peekNext())) {
            is_float = true;
            advance();
            while (isDigit(peek()) || peek() == '_') {
                if (peek() == '_') {
                    advance();
                    if (!isDigit(peek())) {
                        m_errorHandler.report(
                            Token(TokenType::NUMBER_FLOAT, "_", m_line, m_column - 1, m_filename),
                            "Numeric separator '_' must be followed by a digit.", "E014"
                        );
                        return;
                    }
                    continue;
                }
                advance();
            }
        }

        // LANG-6: exponent — e/E [+-]? [0-9_]+
        if (peek() == 'e' || peek() == 'E') {
            is_float = true;
            advance();  // consume 'e' or 'E'
            if (peek() == '+' || peek() == '-') {
                advance();  // consume sign
            }
            if (!isDigit(peek())) {
                m_errorHandler.report(
                    Token(TokenType::NUMBER_FLOAT,
                          m_source.substr(m_start, m_current - m_start),
                          m_line, m_column, m_filename),
                    "Expected digits after exponent.", "E021"
                );
                return;
            }
            while (isDigit(peek()) || peek() == '_') {
                if (peek() == '_') {
                    advance();
                    if (!isDigit(peek())) {
                        m_errorHandler.report(
                            Token(TokenType::NUMBER_FLOAT, "_", m_line, m_column - 1, m_filename),
                            "Numeric separator '_' must be followed by a digit.", "E022"
                        );
                        return;
                    }
                    continue;
                }
                advance();
            }
        }

    suffix_check:
        // LANG-6: numeric type suffix (e.g., 42u8, 0xFFi32).
        // Also strips underscore separators from the lexeme so codegen can
        // parse the number directly (std::stoll / std::stod don't handle '_').
        {
            int number_end = m_current;
            LiteralSuffix suffix = LiteralSuffix::NONE;

            if (isAlpha(peek())) {
                // Consume alphanumeric characters to form the candidate suffix.
                std::string cand;
                int suffix_start = m_current;
                while (isAlphaNumeric(peek())) {
                    cand += advance();
                }

                if (cand == "i8")        suffix = LiteralSuffix::I8;
                else if (cand == "i16")  suffix = LiteralSuffix::I16;
                else if (cand == "i32")  suffix = LiteralSuffix::I32;
                else if (cand == "i64")  suffix = LiteralSuffix::I64;
                else if (cand == "u8")   suffix = LiteralSuffix::U8;
                else if (cand == "u16")  suffix = LiteralSuffix::U16;
                else if (cand == "u32")  suffix = LiteralSuffix::U32;
                else if (cand == "u64")  suffix = LiteralSuffix::U64;
                else {
                    // Invalid suffix — report error. The characters have already
                    // been consumed (they won't appear as a separate token).
                    m_errorHandler.report(
                        Token(is_float ? TokenType::NUMBER_FLOAT : TokenType::NUMBER_INT,
                              m_source.substr(m_start, m_current - m_start),
                              m_line, m_column - static_cast<int>(m_current - suffix_start),
                              m_filename),
                        "Invalid numeric suffix '" + cand + "'.", "E023"
                    );
                }
            }

            // Build clean lexeme without underscore separators.
            std::string raw_num = m_source.substr(m_start, number_end - m_start);
            std::string clean_num;
            clean_num.reserve(raw_num.size());
            for (char ch : raw_num) {
                if (ch != '_') clean_num += ch;
            }
            TokenType emit_type = is_float ? TokenType::NUMBER_FLOAT : TokenType::NUMBER_INT;

            // L5: Validate integer literal range during lexing — don't defer
            // overflow to stoll which may throw. UINT64_MAX = 18446744073709551615.
            if (!is_float && !clean_num.empty()) {
                // Check if the literal is pure decimal (no 0x/0b prefix)
                bool is_hex = (clean_num.size() >= 2 && clean_num[0] == '0' &&
                              (clean_num[1] == 'x' || clean_num[1] == 'X'));
                size_t num_start = is_hex ? 2 : 0;
                std::string digits = clean_num.substr(num_start);
                // Remove any leading zeros for length check
                while (digits.size() > 1 && digits[0] == '0') digits.erase(0, 1);
                if (!is_hex && digits.size() > 20) {
                    m_errorHandler.report(
                        Token(emit_type, clean_num, m_line,
                              m_column - static_cast<int>(m_current - m_start), m_filename),
                        "Integer literal '" + clean_num + "' is too large for a 64-bit integer.",
                        "E024");
                } else if (!is_hex && digits.size() == 20 &&
                           digits > "18446744073709551615") {
                    m_errorHandler.report(
                        Token(emit_type, clean_num, m_line,
                              m_column - static_cast<int>(m_current - m_start), m_filename),
                        "Integer literal '" + clean_num + "' exceeds UINT64_MAX.",
                        "E024");
                }
            }

            if (suffix != LiteralSuffix::NONE || number_end != m_current) {
                // Suffix was found (valid or invalid).  Emit the numeric token
                // with the clean number portion as the lexeme.
                addToken(emit_type, clean_num);
                if (suffix != LiteralSuffix::NONE) {
                    m_tokens.back().suffix = suffix;
                }
            } else {
                // No suffix — emit the token with clean lexeme.
                addToken(emit_type, clean_num);
            }
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
                addToken(match('=') ? TokenType::PERCENT_EQUAL : TokenType::PERCENT);
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

            // LANG-3: interpolated string — $"..."
            case '$':
                if (peek() == '"') {
                    advance(); // consume opening "
                    interpolatedString();
                } else {
                    m_errorHandler.report(
                        Token(TokenType::IDENTIFIER, "$", m_line, m_column - 1, m_filename),
                        "Expected '\"' after '$' for string interpolation.", "E398");
                }
                break;

            case '!':
                addToken(match('=') ? TokenType::BANG_EQUAL : TokenType::BANG);
                break;
            case '=':
                addToken(match('=') ? TokenType::EQUAL_EQUAL : TokenType::EQUAL);
                break;
            case '<':
                if (match('<')) {
                    addToken(match('=') ? TokenType::LSHIFT_EQUAL : TokenType::LSHIFT);
                } else addToken(match('=') ? TokenType::LESS_EQUAL : TokenType::LESS);
                break;
            case '>':
                if (match('>')) {
                    addToken(match('=') ? TokenType::RSHIFT_EQUAL : TokenType::RSHIFT);
                } else addToken(match('=') ? TokenType::GREATER_EQUAL : TokenType::GREATER);
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
                if (match('|')) addToken(TokenType::LOGICAL_OR);
                else addToken(match('=') ? TokenType::PIPE_EQUAL : TokenType::PIPE);
                break;
            case '&':
                if (match('&')) addToken(TokenType::LOGICAL_AND);
                else addToken(match('=') ? TokenType::AMPERSAND_EQUAL : TokenType::AMPERSAND);
                break;
            case '^':
                addToken(match('=') ? TokenType::CARET_EQUAL : TokenType::CARET);
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

            // LANG-4: char literal — 'a', '\n', '\x41'. Exactly one code point.
            case '\'':
                charLiteral();
                break;

            case ' ':
            case '\r':
            case '\t':
                break;
            case '\n':
                m_line++;
                m_column = 1;
                break;

            // LANG-6: raw string — r"..."
            case 'r':
                if (peek() == '"') {
                    advance();  // consume opening "
                    rawString();
                    break;
                }
                identifier();
                break;

            // LANG-6: byte string — b"..."
            case 'b':
                if (peek() == '"') {
                    advance();  // consume opening "
                    byteString();
                    break;
                }
                identifier();
                break;

            default:
                if (isDigit(c)) {
                    number();
                } else if (isAlpha(c)) {
                    identifier();
                } else {
                    m_errorHandler.report(
                        Token(TokenType::IDENTIFIER, std::string(1, c), m_line, m_column - 1, m_filename),
                        "Unexpected character '" + std::string(1, c) + "'.", "E015"
                    );
                }
                break;
        }
    }

}
