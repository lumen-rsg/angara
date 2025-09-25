//
// Created by cv2 on 24.09.2025.
//

#include "LspErrorHandler.h"

namespace angara {

    void LspErrorHandler::report(const Token& token, const std::string& message) {
        // Set the base class's error flag.
        ErrorHandler::report(token, ""); // Call base to set m_hadError, but with empty message to avoid printing.

        // Create and store a structured diagnostic.
        m_diagnostics.push_back(
            create_diagnostic_from_token(token, message, DiagnosticSeverity::Error)
        );
    }

    void LspErrorHandler::note(const Token& token, const std::string& message) {
        // In LSP, a "note" is formally called a "related information" item
        // attached to a primary diagnostic. For simplicity, we will add it as
        // a separate diagnostic with "Information" severity.
        m_diagnostics.push_back(
            create_diagnostic_from_token(token, message, DiagnosticSeverity::Information)
        );
    }

    std::vector<Diagnostic> LspErrorHandler::get_diagnostics() const {
        return m_diagnostics;
    }

} // namespace angara