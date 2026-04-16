//
// Created by cv2 on 8/27/25.
//

#include "ErrorHandler.h"
#include <iostream>
#include <sstream>

const auto CYAN = "\033[36m";
const auto YELLOW = "\033[33m";
const auto RED = "\033[31m";
const auto RESET = "\033[0m";
const auto BOLD = "\033[1m";

namespace angara {

    ErrorHandler::ErrorHandler(const std::string &source) {
        std::stringstream ss(source);
        std::string line;
        while (std::getline(ss, line, '\n')) {
            m_lines.push_back(line);
        }
    }

    void ErrorHandler::report(const Token &token, const std::string &message) {
        m_hadError = true;

        // Standard error header (red)
        std::cerr << BOLD << RED << "[Line " << token.line << "] Error";

        if (token.type == TokenType::EOF_TOKEN) {
            std::cerr << " at end";
        } else {
            std::cerr << " at '" << token.lexeme << "'";
        }
        std::cerr << ": " << RESET << message << std::endl;

        // Print the line with the error
        if (token.line - 1 < m_lines.size()) {
            std::cerr << " " << token.line << " | " << m_lines[token.line - 1] << std::endl;

            // Print the pointer line (e.g., "     ^--- Here")
            std::string pointer;
            // Pad with spaces up to the column
            pointer += "   | " + std::string(token.column - 1, ' ');
            // Use carets for the length of the token
            pointer += std::string(token.lexeme.length() > 0 ? token.lexeme.length() : 1, '^');
            std::cerr << BOLD << RED << pointer << RESET << std::endl;
        }
    }

    void ErrorHandler::warning(const Token &token, const std::string &message) {
        m_hadWarning = true;

        // Warning header (yellow)
        std::cerr << BOLD << YELLOW << "[Line " << token.line << "] Warning";

        if (token.type == TokenType::EOF_TOKEN) {
            std::cerr << " at end";
        } else {
            std::cerr << " at '" << token.lexeme << "'";
        }
        std::cerr << ": " << RESET << message << std::endl;

        // Print the line with the warning
        if (token.line - 1 < m_lines.size()) {
            std::cerr << " " << token.line << " | " << m_lines[token.line - 1] << std::endl;

            std::string pointer;
            pointer += "   | " + std::string(token.column - 1, ' ');
            pointer += std::string(token.lexeme.length() > 0 ? token.lexeme.length() : 1, '^');
            std::cerr << BOLD << YELLOW << pointer << RESET << std::endl;
        }
    }

    void ErrorHandler::note(const Token &token, const std::string &message) {
        // A note is supplemental, so it does not set m_hadError = true.

        std::cerr << BOLD << CYAN << "[Line " << token.line << "] note: " << RESET
                  << message << std::endl;

        // Print the line with the note's context
        if (token.line > 0 && token.line - 1 < m_lines.size()) {
            std::cerr << " " << token.line << " | " << m_lines[token.line - 1] << std::endl;

            // Print the pointer line (e.g., "     ^--- Here")
            std::string pointer;
            pointer += "   | " + std::string(token.column > 0 ? token.column - 1 : 0, ' ');
            pointer += std::string(token.lexeme.length() > 0 ? token.lexeme.length() : 1, '^');
            std::cerr << BOLD << CYAN << pointer << RESET << std::endl;
        }
    }

    bool ErrorHandler::hadError() const {
        return m_hadError;
    }

    bool ErrorHandler::hadWarning() const {
        return m_hadWarning;
    }

    void ErrorHandler::clearError() {
        m_hadError = false;
        m_hadWarning = false;
    }
}