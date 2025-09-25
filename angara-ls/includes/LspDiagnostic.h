//
// Created by cv2 on 24.09.2025.
//

#ifndef ANGARA_LS_LSPDIAGNOSTIC_H
#define ANGARA_LS_LSPDIAGNOSTIC_H

#include <string>
#include "Token.h" // To easily convert a Token to a Diagnostic

namespace angara {

    // Represents the severity of a diagnostic message.
    enum class DiagnosticSeverity {
        Error = 1,
        Warning = 2,
        Information = 3,
        Hint = 4
    };

    // Represents a range in a text document.
    struct Range {
        int start_line;
        int start_char;
        int end_line;
        int end_char;
    };

    // Represents a diagnostic, such as a compiler error or warning.
    struct Diagnostic {
        Range range;
        DiagnosticSeverity severity;
        std::string message;
        // Optional: other LSP fields like 'code' or 'source'
    };

    // Helper function to create a Diagnostic directly from an Angara Token.
    inline Diagnostic create_diagnostic_from_token(const Token& token, const std::string& message, DiagnosticSeverity severity) {
        // LSP positions are 0-indexed, while Angara Tokens are 1-indexed.
        int line = token.line - 1;
        int start_char = token.column - 1;
        int end_char = start_char + token.lexeme.length();

        // For single-token errors, the range is on the same line.
        return Diagnostic{
            {line, start_char, line, end_char},
            severity,
            message
        };
    }

} // namespace angara

#endif // ANGARA_LS_LSPDIAGNOSTIC_H