//
// Created by cv2 on 8/27/25.
//

#include "../include/ErrorHandler.h"
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

    void ErrorHandler::report(const Token &token, const std::string &message) {
        m_hadError = true;

        // Standard error header
        std::cerr << "[Line " << token.line << "] Error";

        if (token.type == TokenType::EOF_TOKEN) {
            std::cerr << " at end";
        } else {
            std::cerr << " at '" << token.lexeme << "'";
        }
        std::cerr << ": " << message << std::endl;

        // Print the line with the error
        if (token.line - 1 < m_lines.size()) {
            std::cerr << " " << token.line << " | " << m_lines[token.line - 1] << std::endl;

            // Print the pointer line (e.g., "     ^--- Here")
            std::string pointer;
            // Pad with spaces up to the column
            pointer += "   | " + std::string(token.column - 1, ' ');
            // Use carets for the length of the token
            pointer += std::string(token.lexeme.length() > 0 ? token.lexeme.length() : 1, '^');
            std::cerr << pointer << std::endl;
        }
    }

    bool ErrorHandler::hadError() const {
        return m_hadError;
    }

    void ErrorHandler::clearError() {
        m_hadError = false;
    }
}