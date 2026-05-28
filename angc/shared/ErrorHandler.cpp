//
// Created by cv2 on 8/27/25.
//

#include "ErrorHandler.h"
#include "Colors.h"
#include <iostream>
#include <sstream>
#include <algorithm>

namespace angara {

    ErrorHandler::ErrorHandler(const std::string &source) {
        std::stringstream ss(source);
        std::string line;
        while (std::getline(ss, line, '\n')) {
            m_lines.push_back(line);
        }
    }

    static std::string json_escape(const std::string &s) {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:   out += c; break;
            }
        }
        return out;
    }

    void ErrorHandler::emit_json(const std::string &severity, const Token &token,
                                  const std::string &message, const std::string &code) const {
        std::string file = token.file ? *token.file : "";
        std::cerr << "{\"severity\":\"" << severity << "\"";
        if (!code.empty()) std::cerr << ",\"code\":\"" << code << "\"";
        std::cerr << ",\"message\":\"" << json_escape(message) << "\"";
        std::cerr << ",\"file\":\"" << json_escape(file) << "\"";
        std::cerr << ",\"line\":" << token.line;
        std::cerr << ",\"column\":" << token.column;
        std::cerr << "}" << std::endl;
    }

    void ErrorHandler::report(const Token &token, const std::string &message, const std::string &code) {
        m_hadError = true;
        m_errorCount++;

        if (m_error_format == "json") {
            emit_json("error", token, message, code);
            return;
        }

        // Standard error header (red)
        std::cerr << CLR_BOLD << CLR_RED << "[Line " << token.line << "] Error";
        if (!code.empty()) {
            std::cerr << " [" << code << "]";
        }

        if (token.type == TokenType::EOF_TOKEN) {
            std::cerr << " at end";
        } else {
            std::cerr << " at '" << token.lexeme << "'";
        }
        std::cerr << ": " << CLR_RESET << message << std::endl;

        // Print the line with the error
        if (token.line - 1 < m_lines.size()) {
            std::cerr << " " << token.line << " | " << m_lines[token.line - 1] << std::endl;

            // Print the pointer line with carets
            std::string pointer;
            pointer += "   | " + std::string(token.column > 0 ? token.column - 1 : 0, ' ');
            pointer += std::string(token.lexeme.length() > 0 ? token.lexeme.length() : 1, '^');
            std::cerr << CLR_BOLD << CLR_RED << pointer << CLR_RESET << std::endl;
        }
    }

    void ErrorHandler::warning(const Token &token, const std::string &message, const std::string &code) {
        // Check if this warning is suppressed
        if (m_suppress_all) return;
        if (!code.empty() && m_suppressed_codes.count(code)) return;

        // Promote to error if -Werror is set
        if (m_warnings_as_errors) {
            report(token, message, code);
            return;
        }

        m_hadWarning = true;
        m_warningCount++;

        if (m_error_format == "json") {
            emit_json("warning", token, message, code);
            return;
        }

        // Warning header (yellow)
        std::cerr << CLR_BOLD << CLR_YELLOW << "[Line " << token.line << "] Warning";
        if (!code.empty()) {
            std::cerr << " [" << code << "]";
        }

        if (token.type == TokenType::EOF_TOKEN) {
            std::cerr << " at end";
        } else {
            std::cerr << " at '" << token.lexeme << "'";
        }
        std::cerr << ": " << CLR_RESET << message << std::endl;

        // Print the line with the warning
        if (token.line - 1 < m_lines.size()) {
            std::cerr << " " << token.line << " | " << m_lines[token.line - 1] << std::endl;

            // Use tildes for warnings (vs carets for errors)
            std::string pointer;
            pointer += "   | " + std::string(token.column > 0 ? token.column - 1 : 0, ' ');
            pointer += std::string(token.lexeme.length() > 0 ? token.lexeme.length() : 1, '~');
            std::cerr << CLR_BOLD << CLR_YELLOW << pointer << CLR_RESET << std::endl;
        }
    }

    void ErrorHandler::report(const Token &start_token, const Token &end_token,
                               const std::string &message, const std::string &code) {
        m_hadError = true;
        m_errorCount++;

        if (m_error_format == "json") {
            emit_json("error", start_token, message, code);
            return;
        }

        // Header
        std::cerr << CLR_BOLD << CLR_RED << "[Line " << start_token.line;
        if (end_token.line != start_token.line) {
            std::cerr << "-" << end_token.line;
        }
        std::cerr << "] Error";
        if (!code.empty()) std::cerr << " [" << code << "]";
        std::cerr << ": " << CLR_RESET << message << std::endl;

        if (start_token.line == end_token.line) {
            // Single-line span — underline from start.lexeme to end.lexeme
            int line_idx = start_token.line - 1;
            if (line_idx >= 0 && line_idx < (int)m_lines.size()) {
                std::cerr << " " << start_token.line << " | " << m_lines[line_idx] << std::endl;
                int start_col = start_token.column > 0 ? start_token.column - 1 : 0;
                int end_col = end_token.column > 0 ? end_token.column - 1 : 0;
                int span_len = end_col - start_col + (int)end_token.lexeme.length();
                if (span_len < 1) span_len = 1;
                std::string pointer;
                pointer += "   | " + std::string(start_col, ' ');
                pointer += std::string(span_len, '^');
                std::cerr << CLR_BOLD << CLR_RED << pointer << CLR_RESET << std::endl;
            }
        } else {
            // Multi-line span
            int first = start_token.line;
            int last = end_token.line;
            if (last > (int)m_lines.size()) last = (int)m_lines.size();

            // Show up to 3 lines before the span start for context
            int show_start = first - 3;
            if (show_start < 1) show_start = 1;

            for (int ln = show_start; ln <= last; ln++) {
                int idx = ln - 1;
                if (idx < 0 || idx >= (int)m_lines.size()) continue;
                std::cerr << " " << ln << " | " << m_lines[idx] << std::endl;

                if (ln >= first && ln <= last) {
                    std::string pointer = "   | ";
                    if (ln == first) {
                        // Underline from start column to end of line
                        int start_col = start_token.column > 0 ? start_token.column - 1 : 0;
                        pointer += std::string(start_col, ' ');
                        int rest = (int)m_lines[idx].length() - start_col;
                        if (rest > 0) pointer += std::string(rest, '^');
                    } else if (ln == last) {
                        // Underline from column 0 to end column
                        int end_col = end_token.column > 0 ? end_token.column - 1 : 0;
                        int span_len = end_col + (int)end_token.lexeme.length();
                        if (span_len < 1) span_len = 1;
                        pointer += std::string(span_len, '^');
                    } else {
                        // Intermediate line: underline the whole line
                        pointer += std::string(m_lines[idx].length(), '^');
                    }
                    std::cerr << CLR_BOLD << CLR_RED << pointer << CLR_RESET << std::endl;
                }
            }
        }
    }

    void ErrorHandler::warning(const Token &start_token, const Token &end_token,
                                const std::string &message, const std::string &code) {
        if (m_suppress_all) return;
        if (!code.empty() && m_suppressed_codes.count(code)) return;

        if (m_warnings_as_errors) {
            report(start_token, end_token, message, code);
            return;
        }

        m_hadWarning = true;
        m_warningCount++;

        if (m_error_format == "json") {
            emit_json("warning", start_token, message, code);
            return;
        }

        // Header
        std::cerr << CLR_BOLD << CLR_YELLOW << "[Line " << start_token.line;
        if (end_token.line != start_token.line) {
            std::cerr << "-" << end_token.line;
        }
        std::cerr << "] Warning";
        if (!code.empty()) std::cerr << " [" << code << "]";
        std::cerr << ": " << CLR_RESET << message << std::endl;

        if (start_token.line == end_token.line) {
            int line_idx = start_token.line - 1;
            if (line_idx >= 0 && line_idx < (int)m_lines.size()) {
                std::cerr << " " << start_token.line << " | " << m_lines[line_idx] << std::endl;
                int start_col = start_token.column > 0 ? start_token.column - 1 : 0;
                int end_col = end_token.column > 0 ? end_token.column - 1 : 0;
                int span_len = end_col - start_col + (int)end_token.lexeme.length();
                if (span_len < 1) span_len = 1;
                std::string pointer;
                pointer += "   | " + std::string(start_col, ' ');
                pointer += std::string(span_len, '~');
                std::cerr << CLR_BOLD << CLR_YELLOW << pointer << CLR_RESET << std::endl;
            }
        } else {
            int first = start_token.line;
            int last = end_token.line;
            if (last > (int)m_lines.size()) last = (int)m_lines.size();

            int show_start = first - 3;
            if (show_start < 1) show_start = 1;

            for (int ln = show_start; ln <= last; ln++) {
                int idx = ln - 1;
                if (idx < 0 || idx >= (int)m_lines.size()) continue;
                std::cerr << " " << ln << " | " << m_lines[idx] << std::endl;

                if (ln >= first && ln <= last) {
                    std::string pointer = "   | ";
                    if (ln == first) {
                        int start_col = start_token.column > 0 ? start_token.column - 1 : 0;
                        pointer += std::string(start_col, ' ');
                        int rest = (int)m_lines[idx].length() - start_col;
                        if (rest > 0) pointer += std::string(rest, '~');
                    } else if (ln == last) {
                        int end_col = end_token.column > 0 ? end_token.column - 1 : 0;
                        int span_len = end_col + (int)end_token.lexeme.length();
                        if (span_len < 1) span_len = 1;
                        pointer += std::string(span_len, '~');
                    } else {
                        pointer += std::string(m_lines[idx].length(), '~');
                    }
                    std::cerr << CLR_BOLD << CLR_YELLOW << pointer << CLR_RESET << std::endl;
                }
            }
        }
    }

    void ErrorHandler::note(const Token &token, const std::string &message) {
        // A note is supplemental, so it does not set m_hadError = true.

        std::cerr << CLR_BOLD << CLR_CYAN << "[Line " << token.line << "] note: " << CLR_RESET
                  << message << std::endl;

        // Print the line with the note's context
        if (token.line > 0 && token.line - 1 < m_lines.size()) {
            std::cerr << " " << token.line << " | " << m_lines[token.line - 1] << std::endl;

            // Print the pointer line (e.g., "     ^--- Here")
            std::string pointer;
            pointer += "   | " + std::string(token.column > 0 ? token.column - 1 : 0, ' ');
            pointer += std::string(token.lexeme.length() > 0 ? token.lexeme.length() : 1, '^');
            std::cerr << CLR_BOLD << CLR_CYAN << pointer << CLR_RESET << std::endl;
        }
    }

    bool ErrorHandler::hadError() const {
        return m_hadError;
    }

    bool ErrorHandler::hadWarning() const {
        return m_hadWarning;
    }

    int ErrorHandler::errorCount() const {
        return m_errorCount;
    }

    int ErrorHandler::warningCount() const {
        return m_warningCount;
    }

    void ErrorHandler::clearError() {
        m_hadError = false;
        m_hadWarning = false;
        m_errorCount = 0;
        m_warningCount = 0;
    }

    void ErrorHandler::printSummary() const {
        if (m_error_format == "json") return;
        if (m_errorCount == 0 && m_warningCount == 0) return;

        std::cerr << CLR_BOLD;
        if (m_errorCount > 0) {
            std::cerr << CLR_RED << m_errorCount << " error" << (m_errorCount > 1 ? "s" : "");
        }
        if (m_errorCount > 0 && m_warningCount > 0) {
            std::cerr << CLR_RESET << ", " << CLR_BOLD;
        }
        if (m_warningCount > 0) {
            std::cerr << CLR_YELLOW << m_warningCount << " warning" << (m_warningCount > 1 ? "s" : "");
        }
        std::cerr << CLR_RESET << std::endl;
    }
}