#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <variant>
#include <sstream>
#include <memory>
#include <functional>

namespace angara {

    // ── Minimal JSON value ────────────────────────────────────
    struct JSON;
    using JSON_NULL = std::nullptr_t;
    using JSON_BOOL = bool;
    using JSON_NUM  = double;
    using JSON_STR  = std::string;
    using JSON_ARR  = std::vector<JSON>;
    using JSON_OBJ  = std::map<std::string, JSON>;
    using JSON_VAL  = std::variant<JSON_NULL, JSON_BOOL, JSON_NUM, JSON_STR, JSON_ARR, JSON_OBJ>;

    struct JSON : JSON_VAL {
        JSON() : JSON_VAL(nullptr) {}
        JSON(std::nullptr_t) : JSON_VAL(nullptr) {}
        JSON(bool b) : JSON_VAL(b) {}
        JSON(int n) : JSON_VAL(static_cast<double>(n)) {}
        JSON(long n) : JSON_VAL(static_cast<double>(n)) {}
        JSON(long long n) : JSON_VAL(static_cast<double>(n)) {}
        JSON(double n) : JSON_VAL(n) {}
        JSON(const char* s) : JSON_VAL(std::string(s)) {}
        JSON(const std::string& s) : JSON_VAL(s) {}
        JSON(std::string&& s) : JSON_VAL(std::move(s)) {}
        JSON(const JSON_ARR& a) : JSON_VAL(a) {}
        JSON(JSON_ARR&& a) : JSON_VAL(std::move(a)) {}
        JSON(const JSON_OBJ& o) : JSON_VAL(o) {}
        JSON(JSON_OBJ&& o) : JSON_VAL(std::move(o)) {}

        // Type checks
        bool is_null() const { return std::holds_alternative<JSON_NULL>(*this); }
        bool is_bool() const { return std::holds_alternative<JSON_BOOL>(*this); }
        bool is_num() const { return std::holds_alternative<JSON_NUM>(*this); }
        bool is_str() const { return std::holds_alternative<JSON_STR>(*this); }
        bool is_arr() const { return std::holds_alternative<JSON_ARR>(*this); }
        bool is_obj() const { return std::holds_alternative<JSON_OBJ>(*this); }

        // Accessors (throw on wrong type)
        bool as_bool() const { return std::get<JSON_BOOL>(*this); }
        double as_num() const { return std::get<JSON_NUM>(*this); }
        int as_int() const { return static_cast<int>(std::get<JSON_NUM>(*this)); }
        const std::string& as_str() const { return std::get<JSON_STR>(*this); }
        const JSON_ARR& as_arr() const { return std::get<JSON_ARR>(*this); }
        const JSON_OBJ& as_obj() const { return std::get<JSON_OBJ>(*this); }
        JSON_ARR& as_arr_mut() { return std::get<JSON_ARR>(*this); }
        JSON_OBJ& as_obj_mut() { return std::get<JSON_OBJ>(*this); }

        // Object access
        const JSON& operator[](const std::string& key) const {
            auto& obj = std::get<JSON_OBJ>(*this);
            auto it = obj.find(key);
            if (it == obj.end()) throw std::runtime_error("JSON key not found: " + key);
            return it->second;
        }
        JSON& operator[](const std::string& key) {
            if (!is_obj()) *this = JSON_OBJ{};
            return std::get<JSON_OBJ>(*this)[key];
        }
        bool has(const std::string& key) const {
            if (!is_obj()) return false;
            auto& obj = std::get<JSON_OBJ>(*this);
            return obj.find(key) != obj.end();
        }

        // Array access
        const JSON& operator[](size_t idx) const { return std::get<JSON_ARR>(*this).at(idx); }
        size_t size() const {
            if (is_arr()) return std::get<JSON_ARR>(*this).size();
            if (is_obj()) return std::get<JSON_OBJ>(*this).size();
            return 0;
        }

        // Serialize
        std::string dump() const;
        static JSON parse(const std::string& s);
    };

    // ── LSP Diagnostics ───────────────────────────────────────

    struct LSPRange {
        int startLine = 0, startChar = 0;
        int endLine = 0, endChar = 0;
    };

    struct LSPDiagnostic {
        int severity = 1; // 1=Error, 2=Warning, 3=Info, 4=Hint
        std::string code;
        std::string message;
        LSPRange range;
        std::string source = "angara";
    };

    struct LSPCompletionItem {
        std::string label;
        int kind = 6; // 6=Variable, 3=Function, 7=Class, etc.
        std::string detail;
    };

    struct LSPLocation {
        std::string uri;
        LSPRange range;
    };

    /// A symbol reference at a source position, used for hover/definition lookup.
    struct SymbolRef {
        std::string name;
        std::string typeString;
        int line = 0, startCol = 0, endCol = 0; // 0-based LSP position
        std::string declFile;                    // for go-to-definition
        int declLine = 0, declCol = 0;           // 0-based LSP position
    };

    /// A name-indexed symbol table entry, used for fallback hover/definition.
    struct SymbolTableEntry {
        std::string typeString;
        std::string declFile;
        int declLine = 0, declCol = 0; // 0-based LSP position
    };

    /// Tracks a call expression for signature help.
    struct CallSiteInfo {
        std::string calleeName;
        std::string signature;   // e.g. "function(i64, string) -> bool"
        int openParenLine = 0;   // 0-based line of the '(' token
        int openParenCol = 0;    // 0-based column of the '(' token
    };

    /// A top-level document symbol (for outline/breadcrumbs).
    struct DocumentSymbolInfo {
        std::string name;
        int kind;                // LSP SymbolKind: 12=Function, 5=Class, 23=Struct, 10=Enum, 22=Interface
        int startLine = 0, startCol = 0;
        int endLine = 0, endCol = 0;
        std::string detail;      // type string or similar
    };

    // ── LSP Server ────────────────────────────────────────────

    class LSPServer {
    public:
        int run();

    private:
        // State types
        struct DocumentState {
            std::string path;
            std::string source;
            int version = 0;
        };

        struct AnalysisResult {
            std::vector<LSPDiagnostic> diagnostics;
            std::vector<LSPCompletionItem> completions;
            std::vector<SymbolRef> symbols;
            std::map<std::string, SymbolTableEntry> symbolTable;
            std::vector<CallSiteInfo> callSites;
            std::vector<DocumentSymbolInfo> documentSymbols;
            std::vector<std::shared_ptr<const class Stmt>> topLevelStmts;
        };

        // JSON-RPC transport
        std::string readMessage();
        void sendMessage(const JSON& msg);
        void sendResponse(const JSON& id, const JSON& result);
        void sendError(const JSON& id, int code, const std::string& msg);
        void notify(const std::string& method, const JSON& params);

        // Request handlers
        JSON handleInitialize(const JSON& params);
        void handleInitialized(const JSON& params);
        void handleDidOpen(const JSON& params);
        void handleDidChange(const JSON& params);
        void handleDidClose(const JSON& params);
        JSON handleCompletion(const JSON& params);
        JSON handleHover(const JSON& params);
        JSON handleDefinition(const JSON& params);
        JSON handleSignatureHelp(const JSON& params);
        JSON handleDocumentSymbol(const JSON& params);
        JSON handleShutdown();
        void handleExit();

        // Analysis
        void analyzeDocument(const std::string& uri);
        void publishDiagnostics(const std::string& uri);
        std::string uriToPath(const std::string& uri);
        std::string pathToUri(const std::string& path);
        void buildSymbolCache(AnalysisResult& result, class TypeChecker& typeChecker,
                              const std::vector<std::shared_ptr<const class Stmt>>& stmts,
                              const std::string& docPath);
        static std::string extractWordAt(const std::string& source, int line, int col);

        // State
        std::map<std::string, DocumentState> m_documents;
        std::string m_workspace_root;
        bool m_shutdown = false;
        std::map<std::string, AnalysisResult> m_analysis;
    };

} // namespace angara
