#ifndef ANGARA_LS_LANGUAGESERVERSTATE_H
#define ANGARA_LS_LANGUAGESERVERSTATE_H

#include "DocumentManager.h"
#include "VirtualCompilerDriver.h"
#include "LspDiagnostic.h"       // We still need this
#include "TypeChecker.h"         // We need the TypeChecker's definition
#include <map>

namespace angara {
    struct Position;


    // A struct to hold the results of a successful compilation pass.
    struct AnalysisResult {
        std::vector<std::shared_ptr<Stmt>> statements;
        std::shared_ptr<TypeChecker> type_checker;
    };

    class LanguageServerState {
    public:
        LanguageServerState();

        void on_document_open(const std::string& uri, const std::string& content);
        void on_document_change(const std::string& uri, const std::string& content);
        void on_document_close(const std::string& uri);

        // This method now re-runs analysis and returns diagnostics.
        std::vector<Diagnostic> get_diagnostics(const std::string& uri);

        // --- NEW PUBLIC METHOD FOR HOVER ---
        // Takes a position and returns a formatted string for the hover tooltip.
        std::optional<std::string> get_hover_info(const std::string& uri, const Position& position);

    private:
        // --- NEW: Re-runs analysis for a document ---
        void analyze_document(const std::string& path, const std::string& content);

        DocumentManager m_document_manager;
        VirtualCompilerDriver m_driver;

        // --- NEW: A cache for our analysis results ---
        // Maps a file path to its latest successful analysis.
        std::map<std::string, AnalysisResult> m_analysis_cache;
    };

} // namespace angara

#endif // ANGARA_LS_LANGUAGESERVERSTATE_H