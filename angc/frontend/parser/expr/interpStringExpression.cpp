#include "Parser.h"
#include "Lexer.h"
namespace angara {

    // LANG-3: parse an interpolated string body into literal + expression segments.
    // The lexer emitted a single INTERP_STRING token whose lexeme is the raw body
    // (between the $" and closing "). We split it here:
    //   - literal segments: accumulate chars, processing escape sequences (\n, \t, etc.)
    //   - expression holes: on unescaped '{', scan to matching '}' (tracking depth),
    //     then re-lex + re-parse the substring as an Angara expression.
    std::shared_ptr<Expr> Parser::parseInterpolatedString(const Token& tok) {
        const std::string& body = tok.lexeme;
        std::vector<std::pair<std::string, std::shared_ptr<Expr>>> segments;
        std::string current_literal;
        size_t i = 0;

        auto flush_literal = [&]() {
            segments.push_back({current_literal, nullptr});
            current_literal.clear();
        };

        while (i < body.size()) {
            char c = body[i];

            // Escape sequence — process it into the literal.
            if (c == '\\' && i + 1 < body.size()) {
                char esc = body[i + 1];
                switch (esc) {
                    case 'n':  current_literal += '\n'; break;
                    case 't':  current_literal += '\t'; break;
                    case 'r':  current_literal += '\r'; break;
                    case '\\': current_literal += '\\'; break;
                    case '"':  current_literal += '"';  break;
                    case '0':  current_literal += '\0'; break;
                    case 'b':  current_literal += '\b'; break;
                    case 'f':  current_literal += '\f'; break;
                    case 'v':  current_literal += '\v'; break;
                    case 'a':  current_literal += '\a'; break;
                    default:   current_literal += esc;  break;
                }
                i += 2;
                continue;
            }

            // Start of an expression hole '{'.
            if (c == '{') {
                flush_literal();
                i++; // skip '{'

                // Scan to the matching '}' (tracking brace depth for nested {},
                // and skipping over string literals so a '}' or '{' inside a
                // string doesn't unbalance the count).
                std::string expr_src;
                int depth = 1;
                while (i < body.size() && depth > 0) {
                    char ec = body[i];
                    if (ec == '"') {
                        // Copy a nested string literal verbatim.
                        expr_src += ec;
                        i++;
                        while (i < body.size() && body[i] != '"') {
                            if (body[i] == '\\' && i + 1 < body.size()) {
                                expr_src += body[i++];
                                expr_src += body[i];
                            } else {
                                expr_src += body[i];
                            }
                            i++;
                        }
                        if (i < body.size() && body[i] == '"') {
                            expr_src += body[i];
                            i++;
                        }
                        continue;
                    }
                    if (ec == '{') {
                        depth++;
                        expr_src += ec;
                    } else if (ec == '}') {
                        depth--;
                        if (depth == 0) break;
                        expr_src += ec;
                    } else if (ec == '\\' && i + 1 < body.size()) {
                        // Keep escapes verbatim in the expression source.
                        expr_src += ec;
                        expr_src += body[i + 1];
                        i++;
                    } else {
                        expr_src += ec;
                    }
                    i++;
                }

                if (depth != 0) {
                    error(tok, "Unterminated expression hole '{' in interpolated string — missing '}'.", "E400");
                    // Emit a nil expression so compilation continues.
                    segments.push_back({"", std::make_shared<Literal>(
                        Token(TokenType::NIL, "nil", tok.line, tok.column, tok.file))});
                    break;
                }
                i++; // skip '}'

                // Re-lex and parse the expression source.
                if (!expr_src.empty()) {
                    Lexer lexer(expr_src, tok.file, m_errorHandler);
                    auto sub_tokens = lexer.scanTokens();
                    if (!sub_tokens.empty()) {
                        Parser sub_parser(sub_tokens, m_errorHandler);
                        try {
                            auto expr = sub_parser.expression();
                            segments.push_back({"", expr});
                        } catch (...) {
                            // Error already reported by the sub-parser.
                            segments.push_back({"", std::make_shared<Literal>(
                                Token(TokenType::NIL, "nil", tok.line, tok.column, tok.file))});
                        }
                    } else {
                        segments.push_back({"", std::make_shared<Literal>(
                            Token(TokenType::NIL, "nil", tok.line, tok.column, tok.file))});
                    }
                } else {
                    // Empty hole {} — treat as nil.
                    segments.push_back({"", std::make_shared<Literal>(
                        Token(TokenType::NIL, "nil", tok.line, tok.column, tok.file))});
                }
                continue;
            }

            // Regular character.
            current_literal += c;
            i++;
        }

        // Flush the trailing literal segment.
        flush_literal();

        return std::make_shared<InterpStringExpr>(std::move(segments));
    }

}
