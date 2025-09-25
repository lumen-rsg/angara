#ifndef ANGARA_LS_LSPDIAGNOSTIC_H
#define ANGARA_LS_LSPDIAGNOSTIC_H

#include <string>
#include "Token.h"

namespace angara {

    // Represents a cursor position in a text document (0-indexed).
    struct Position {
        int line;
        int character;
    };

    // Represents a range in a text document.
    struct Range {
        Position start;
        Position end;
    };

    // --- END NEW ---

    enum class DiagnosticSeverity {
        Error = 1,
        Warning = 2,
        Information = 3,
        Hint = 4
    };

    struct Diagnostic {
        Range range; // This will now use our central Range struct
        DiagnosticSeverity severity;
        std::string message;
    };

    // Helper function to create a Diagnostic directly from an Angara Token.
    inline Diagnostic create_diagnostic_from_token(const Token& token, const std::string& message, DiagnosticSeverity severity) {
        int line = token.line - 1;
        int start_char = token.column - 1;
        int end_char = start_char + token.lexeme.length();

        Position start_pos = {line, start_char};
        Position end_pos = {line, end_char};

        return Diagnostic{
            {start_pos, end_pos}, // Use the correct Range structure
            severity,
            message
        };
    }

} // namespace angara

#endif // ANGARA_LS_LSPDIAGNOSTIC_H