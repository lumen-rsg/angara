//
// Created by cv2 on 24.09.2025.
//

#include "LanguageServerState.h"

#include <LspErrorHandler.h>

#include "Lexer.h"    // We need the whole pipeline
#include "Parser.h"
#include "TypeChecker.h"

namespace angara {

// --- LanguageServerState Implementation ---

LanguageServerState::LanguageServerState()
    : m_driver(m_document_manager) // Initialize the driver with a reference to the manager
{}

void LanguageServerState::on_document_open(const std::string& uri, const std::string& content) {
    m_document_manager.on_open(uri, content);
}

void LanguageServerState::on_document_change(const std::string& uri, const std::string& content) {
    m_document_manager.on_change(uri, content);
}

void LanguageServerState::on_document_close(const std::string& uri) {
    m_document_manager.on_close(uri);
}

    std::vector<Diagnostic> LanguageServerState::get_diagnostics(const std::string& uri) {
        const auto path = uri_to_path(uri);
        const auto content = m_document_manager.get_content(path);
        if (!content) {
            return {}; // Document isn't open, no diagnostics to provide.
        }

        // --- The Complete, Working Virtual Compilation Pipeline ---

        // 1. Create our new LspErrorHandler.
        LspErrorHandler diagnostics_collector(*content);

        // 2. Run the full frontend and semantic analysis.
        // The compiler core will now call our overridden `report` and `note`
        // methods, populating our diagnostics vector automatically.
        Lexer lexer(*content);
        const auto tokens = lexer.scanTokens();

        Parser parser(tokens, diagnostics_collector);
        const auto statements = parser.parseStmts();

        // We can stop after parsing if there are syntax errors.
        if (diagnostics_collector.hadError()) {
            return diagnostics_collector.get_diagnostics();
        }

        // The module name for a virtual file can be derived from its path.
        const std::string module_name = CompilerDriver::get_base_name(path);
        TypeChecker typeChecker(m_driver, diagnostics_collector, module_name);
        typeChecker.check(statements);

        // 3. Return the collected diagnostics.
        return diagnostics_collector.get_diagnostics();
    }


} // namespace angara
