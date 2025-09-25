//
// Created by cv2 on 24.09.2025.
//

#ifndef ANGARA_LS_LANGUAGESERVERSTATE_H
#define ANGARA_LS_LANGUAGESERVERSTATE_H

#include "DocumentManager.h"
#include "VirtualCompilerDriver.h" // Your new driver
#include "ErrorHandler.h"          // We'll need to adapt this next
#include "LspDiagnostic.h"

namespace angara {

// A container for all the state and logic of the language server.
class LanguageServerState {
public:
    LanguageServerState();

    // Handles document notifications from the editor
    void on_document_open(const std::string& uri, const std::string& content);
    void on_document_change(const std::string& uri, const std::string& content);
    void on_document_close(const std::string& uri);

    // Runs the analysis and returns a list of diagnostics (errors/warnings)
    // This will be the core function called by your LSP message handlers.
    std::vector<Diagnostic> get_diagnostics(const std::string& uri);

private:
    DocumentManager m_document_manager;
    VirtualCompilerDriver m_driver;
};

} // namespace angara

#endif // ANGARA_LS_LANGUAGESERVERSTATE_H```
