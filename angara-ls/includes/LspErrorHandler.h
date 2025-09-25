//
// Created by cv2 on 24.09.2025.
//

#ifndef ANGARA_LS_LSPERRORHANDLER_H
#define ANGARA_LS_LSPERRORHANDLER_H

#include "ErrorHandler.h" // The base class from your compiler core
#include "LspDiagnostic.h"
#include <vector>

namespace angara {

    class LspErrorHandler : public ErrorHandler {
    public:
        // We inherit the constructor from the base class.
        using ErrorHandler::ErrorHandler;

        // Override the core reporting methods.
        void report(const Token& token, const std::string& message) override;
        void note(const Token& token, const std::string& message) override;

        // A new method to retrieve the collected results.
        std::vector<Diagnostic> get_diagnostics() const;

    private:
        std::vector<Diagnostic> m_diagnostics;
    };

} // namespace angara

#endif // ANGARA_LS_LSPERRORHANDLER_H
