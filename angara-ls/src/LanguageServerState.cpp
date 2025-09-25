#include "LanguageServerState.h"
#include "LspErrorHandler.h"
#include "Lexer.h"
#include "Parser.h"
#include "TypeChecker.h"
#include "AstNodeFinder.h" // <-- INCLUDE OUR NEW FINDER
#include <vector>

namespace angara {

LanguageServerState::LanguageServerState()
    : m_driver(m_document_manager)
{}

void LanguageServerState::on_document_open(const std::string& uri, const std::string& content) {
    m_document_manager.on_open(uri, content);
    // When a document is opened, immediately analyze it.
    analyze_document(uri_to_path(uri), content);
}

void LanguageServerState::on_document_change(const std::string& uri, const std::string& content) {
    m_document_manager.on_change(uri, content);
    // When a document changes, immediately re-analyze it.
    analyze_document(uri_to_path(uri), content);
}

void LanguageServerState::on_document_close(const std::string& uri) {
    m_document_manager.on_close(uri);
    // When a document is closed, clear its analysis cache.
    m_analysis_cache.erase(uri_to_path(uri));
}

void LanguageServerState::analyze_document(const std::string& path, const std::string& content) {
    LspErrorHandler error_handler(content);

    Lexer lexer(content);
    auto tokens = lexer.scanTokens();

    Parser parser(tokens, error_handler);
    auto statements = parser.parseStmts();

    if (error_handler.hadError()) {
        // If there's a syntax error, clear any old successful analysis but don't store a new one.
        m_analysis_cache.erase(path);
        return;
    }

    std::string module_name = CompilerDriver::get_base_name(path);
    auto type_checker = std::make_shared<TypeChecker>(m_driver, error_handler, module_name);

    type_checker->check(statements);

    // Even if there are type errors, we still store the result, as we might
    // be able to provide partial hover info.
    m_analysis_cache[path] = {statements, type_checker};
}

std::vector<Diagnostic> LanguageServerState::get_diagnostics(const std::string& uri) {
    auto path = uri_to_path(uri);
    auto content = m_document_manager.get_content(path);
    if (!content) return {};

    // Run a fresh analysis pass.
    analyze_document(path, *content);

    // The error handler for the analysis is created inside analyze_document,
    // so we need to run it again, but this time just to get the diagnostics.
    // This is slightly inefficient but simple.
    LspErrorHandler diagnostics_collector(*content);
    Lexer lexer(*content);
    auto tokens = lexer.scanTokens();
    Parser parser(tokens, diagnostics_collector);
    auto statements = parser.parseStmts();
    if(diagnostics_collector.hadError()) return diagnostics_collector.get_diagnostics();

    TypeChecker typeChecker(m_driver, diagnostics_collector, CompilerDriver::get_base_name(path));
    typeChecker.check(statements);

    return diagnostics_collector.get_diagnostics();
}

// --- THE NEW HOVER LOGIC ---
std::optional<std::string> LanguageServerState::get_hover_info(const std::string& uri, const Position& position) {
    auto path = uri_to_path(uri);

    // 1. Look up the cached analysis for this document.
    auto it = m_analysis_cache.find(path);
    if (it == m_analysis_cache.end()) {
        return std::nullopt; // No successful analysis available.
    }

    const auto& analysis = it->second;

    // 2. Use our AstNodeFinder to find the expression under the cursor.
    AstNodeFinder finder;
    auto found_expr = finder.find(analysis.statements, position);

    if (!found_expr) {
        return std::nullopt; // Nothing found at this position.
    }

    // 3. Look up the type of the found expression from the cached TypeChecker.
    auto type_checker = analysis.type_checker;
    auto type_it = type_checker->m_expression_types.find(found_expr.get());
    if (type_it == type_checker->m_expression_types.end()) {
        return std::nullopt; // Could not find a type for this expression.
    }

    auto type = type_it->second;

    // 4. Format the result for display in VS Code.
    // We can use markdown for better formatting.
    std::string hover_content = "```angara\n";
    hover_content += "(variable) "; // A placeholder, we can improve this
    hover_content += type->toString();
    hover_content += "\n```";

    return hover_content;
}

} // namespace angara