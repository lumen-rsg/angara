//
// Created by cv2 on 8/27/25.
//

#include "ErrorHandler.h"
#include "Colors.h"
#include <iostream>
#include <sstream>

namespace angara {

    ErrorHandler::ErrorHandler(const std::string &source) {
        std::stringstream ss(source);
        std::string line;
        while (std::getline(ss, line, '\n')) {
            m_lines.push_back(line);
        }
    }

    void ErrorHandler::report(const Token &token, const std::string &message, const std::string &code) {
        m_hadError = true;
        m_errorCount++;

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