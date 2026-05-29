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

    // ── LSP Server ────────────────────────────────────────────

    class LSPServer {
    public:
        int run();

    private:
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
        JSON handleShutdown();
        void handleExit();

        // Analysis
        void analyzeDocument(const std::string& uri);
        void publishDiagnostics(const std::string& uri);
        std::string uriToPath(const std::string& uri);
        std::string pathToUri(const std::string& path);

        // State
        struct DocumentState {
            std::string path;
            std::string source;
            int version = 0;
        };
        std::map<std::string, DocumentState> m_documents;
        bool m_shutdown = false;

        // Analysis results per document
        struct AnalysisResult {
            std::vector<LSPDiagnostic> diagnostics;
            // Maps for completion, hover, definition
            std::vector<LSPCompletionItem> completions;
        };
        std::map<std::string, AnalysisResult> m_analysis;
    };

} // namespace angara
