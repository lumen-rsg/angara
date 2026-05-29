#include "LSPServer.h"
#include "Lexer.h"
#include "Parser.h"
#include "TypeChecker.h"
#include "CompilerDriver.h"
#include "ErrorHandler.h"
#include "SymbolTable.h"
#include "Type.h"
#include "Stmt.h"
#include "Expr.h"

#include <iostream>
#include <filesystem>
#include <algorithm>
#include <sstream>

namespace fs = std::filesystem;

namespace angara {

// ── JSON Serialization ────────────────────────────────────────

std::string JSON::dump() const {
    struct Dumper {
        std::string operator()(JSON_NULL) const { return "null"; }
        std::string operator()(bool b) const { return b ? "true" : "false"; }
        std::string operator()(double n) const {
            if (n == static_cast<double>(static_cast<long long>(n)) && n < 1e15 && n > -1e15) {
                return std::to_string(static_cast<long long>(n));
            }
            std::ostringstream oss;
            oss << n;
            return oss.str();
        }
        std::string operator()(const std::string& s) const {
            std::string r = "\"";
            for (char c : s) {
                switch (c) {
                    case '"':  r += "\\\""; break;
                    case '\\': r += "\\\\"; break;
                    case '\n': r += "\\n"; break;
                    case '\r': r += "\\r"; break;
                    case '\t': r += "\\t"; break;
                    default:
                        if (static_cast<unsigned char>(c) < 0x20) {
                            char buf[8];
                            snprintf(buf, sizeof(buf), "\\u%04x", c);
                            r += buf;
                        } else {
                            r += c;
                        }
                }
            }
            return r + "\"";
        }
        std::string operator()(const JSON_ARR& a) const {
            std::string r = "[";
            for (size_t i = 0; i < a.size(); i++) {
                if (i) r += ",";
                r += std::visit(*this, static_cast<const JSON_VAL&>(a[i]));
            }
            return r + "]";
        }
        std::string operator()(const JSON_OBJ& o) const {
            std::string r = "{";
            bool first = true;
            for (auto& [k, v] : o) {
                if (!first) r += ",";
                first = false;
                r += std::visit(*this, JSON(k));
                r += ":";
                r += std::visit(*this, static_cast<const JSON_VAL&>(v));
            }
            return r + "}";
        }
    };
    return std::visit(Dumper{}, static_cast<const JSON_VAL&>(*this));
}

// ── JSON Parsing ───────────────────────────────────────────────

class JSONParser {
    const std::string& src;
    size_t pos = 0;

    void skipWS() {
        while (pos < src.size() && (src[pos] == ' ' || src[pos] == '\t' || src[pos] == '\n' || src[pos] == '\r'))
            pos++;
    }

    JSON parseValue() {
        skipWS();
        if (pos >= src.size()) throw std::runtime_error("Unexpected end of JSON");
        char c = src[pos];
        if (c == '"') return parseString();
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == 't' || c == 'f') return parseBool();
        if (c == 'n') return parseNull();
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
        throw std::runtime_error(std::string("Unexpected char: ") + c);
    }

    JSON parseString() {
        pos++; // skip opening "
        std::string result;
        while (pos < src.size() && src[pos] != '"') {
            if (src[pos] == '\\') {
                pos++;
                if (pos >= src.size()) break;
                switch (src[pos]) {
                    case '"':  result += '"'; break;
                    case '\\': result += '\\'; break;
                    case '/':  result += '/'; break;
                    case 'n':  result += '\n'; break;
                    case 'r':  result += '\r'; break;
                    case 't':  result += '\t'; break;
                    case 'b':  result += '\b'; break;
                    case 'f':  result += '\f'; break;
                    case 'u': {
                        pos++; // skip 'u'
                        if (pos + 3 >= src.size()) { result += 'u'; break; }
                        unsigned cp = 0;
                        for (int i = 0; i < 4; i++) {
                            char h = src[pos + i];
                            cp = cp * 16 + (h >= '0' && h <= '9' ? h - '0' :
                                            h >= 'a' && h <= 'f' ? h - 'a' + 10 :
                                            h >= 'A' && h <= 'F' ? h - 'A' + 10 : 0);
                        }
                        pos += 3;
                        if (cp < 0x80) result += static_cast<char>(cp);
                        else if (cp < 0x800) {
                            result += static_cast<char>(0xC0 | (cp >> 6));
                            result += static_cast<char>(0x80 | (cp & 0x3F));
                        } else {
                            result += static_cast<char>(0xE0 | (cp >> 12));
                            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            result += static_cast<char>(0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                    default: result += src[pos]; break;
                }
            } else {
                result += src[pos];
            }
            pos++;
        }
        if (pos < src.size()) pos++; // skip closing "
        return JSON(std::move(result));
    }

    JSON parseNumber() {
        size_t start = pos;
        if (src[pos] == '-') pos++;
        while (pos < src.size() && src[pos] >= '0' && src[pos] <= '9') pos++;
        if (pos < src.size() && src[pos] == '.') {
            pos++;
            while (pos < src.size() && src[pos] >= '0' && src[pos] <= '9') pos++;
        }
        if (pos < src.size() && (src[pos] == 'e' || src[pos] == 'E')) {
            pos++;
            if (pos < src.size() && (src[pos] == '+' || src[pos] == '-')) pos++;
            while (pos < src.size() && src[pos] >= '0' && src[pos] <= '9') pos++;
        }
        double val = std::stod(src.substr(start, pos - start));
        return JSON(val);
    }

    JSON parseBool() {
        if (src.substr(pos, 4) == "true") { pos += 4; return JSON(true); }
        if (src.substr(pos, 5) == "false") { pos += 5; return JSON(false); }
        throw std::runtime_error("Invalid boolean");
    }

    JSON parseNull() {
        if (src.substr(pos, 4) == "null") { pos += 4; return JSON(nullptr); }
        throw std::runtime_error("Invalid null");
    }

    JSON parseArray() {
        pos++; // skip [
        JSON_ARR arr;
        skipWS();
        if (pos < src.size() && src[pos] == ']') { pos++; return JSON(std::move(arr)); }
        while (true) {
            arr.push_back(parseValue());
            skipWS();
            if (pos < src.size() && src[pos] == ',') { pos++; continue; }
            if (pos < src.size() && src[pos] == ']') { pos++; break; }
            throw std::runtime_error("Expected ',' or ']'");
        }
        return JSON(std::move(arr));
    }

    JSON parseObject() {
        pos++; // skip {
        JSON_OBJ obj;
        skipWS();
        if (pos < src.size() && src[pos] == '}') { pos++; return JSON(std::move(obj)); }
        while (true) {
            skipWS();
            JSON key = parseString();
            skipWS();
            if (pos < src.size() && src[pos] == ':') pos++;
            else throw std::runtime_error("Expected ':'");
            JSON val = parseValue();
            obj[key.as_str()] = std::move(val);
            skipWS();
            if (pos < src.size() && src[pos] == ',') { pos++; continue; }
            if (pos < src.size() && src[pos] == '}') { pos++; break; }
            throw std::runtime_error("Expected ',' or '}'");
        }
        return JSON(std::move(obj));
    }

public:
    explicit JSONParser(const std::string& s) : src(s) {}
    JSON parse() { return parseValue(); }
};

JSON JSON::parse(const std::string& s) {
    return JSONParser(s).parse();
}

// ── URI utilities ──────────────────────────────────────────────

static std::string urlEncode(const std::string& s) {
    std::string r;
    for (char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '/' || c == '_' || c == '.' || c == '-' || c == '~') {
            r += c;
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", static_cast<unsigned char>(c));
            r += buf;
        }
    }
    return r;
}

static std::string urlDecode(const std::string& s) {
    std::string r;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) {
            char hex[3] = {s[i+1], s[i+2], 0};
            r += static_cast<char>(strtol(hex, nullptr, 16));
            i += 2;
        } else {
            r += s[i];
        }
    }
    return r;
}

std::string LSPServer::uriToPath(const std::string& uri) {
    if (uri.starts_with("file://")) {
        std::string path = uri.substr(7);
#ifdef __APPLE__
        // macOS: file:///path -> /path
        if (!path.empty() && path[0] != '/') {
            // Strip host segment
            auto slash = path.find('/');
            if (slash != std::string::npos) path = path.substr(slash);
        }
#endif
        return urlDecode(path);
    }
    return uri;
}

std::string LSPServer::pathToUri(const std::string& path) {
    return "file://" + urlEncode(fs::absolute(path).string());
}

// ── JSON-RPC Transport ─────────────────────────────────────────

std::string LSPServer::readMessage() {
    // Read Content-Length header
    std::string header;
    while (true) {
        int c = std::cin.get();
        if (c == EOF) return "";
        header += static_cast<char>(c);
        if (header.size() >= 4 && header.substr(header.size() - 4) == "\r\n\r\n") break;
    }
    // Parse Content-Length
    size_t cl = header.find("Content-Length: ");
    if (cl == std::string::npos) return "";
    size_t len_start = cl + 16;
    size_t len_end = header.find('\r', len_start);
    int length = std::stoi(header.substr(len_start, len_end - len_start));

    // Read body
    std::string body(length, '\0');
    std::cin.read(body.data(), length);
    if (std::cin.gcount() != length) return "";
    return body;
}

void LSPServer::sendMessage(const JSON& msg) {
    std::string body = msg.dump();
    std::cout << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    std::cout.flush();
}

void LSPServer::sendResponse(const JSON& id, const JSON& result) {
    JSON resp = JSON_OBJ{};
    resp["jsonrpc"] = "2.0";
    resp["id"] = id;
    resp["result"] = result;
    sendMessage(resp);
}

void LSPServer::sendError(const JSON& id, int code, const std::string& msg) {
    JSON err = JSON_OBJ{};
    err["code"] = code;
    err["message"] = msg;
    JSON resp = JSON_OBJ{};
    resp["jsonrpc"] = "2.0";
    resp["id"] = id;
    resp["error"] = err;
    sendMessage(resp);
}

void LSPServer::notify(const std::string& method, const JSON& params) {
    JSON notif = JSON_OBJ{};
    notif["jsonrpc"] = "2.0";
    notif["method"] = method;
    notif["params"] = params;
    sendMessage(notif);
}

// ── Diagnostic Collection ──────────────────────────────────────

namespace {
    // Subclass ErrorHandler to collect diagnostics instead of printing
    class DiagnosticsCollector : public ErrorHandler {
    public:
        explicit DiagnosticsCollector(const std::string& source)
            : ErrorHandler(source) {}

        std::vector<LSPDiagnostic> diagnostics;

        void report(const Token &token, const std::string &message, const std::string &code = "") override {
            ErrorHandler::report(token, message, code);
            addDiagnostic(1, token, message, code);
        }

        void warning(const Token &token, const std::string &message, const std::string &code = "") override {
            ErrorHandler::warning(token, message, code);
            addDiagnostic(2, token, message, code);
        }

        void report(const Token &start, const Token &end, const std::string &message, const std::string &code = "") override {
            ErrorHandler::report(start, end, message, code);
            addDiagnostic(1, start, end, message, code);
        }

        void warning(const Token &start, const Token &end, const std::string &message, const std::string &code = "") override {
            ErrorHandler::warning(start, end, message, code);
            addDiagnostic(2, start, end, message, code);
        }

    private:
        void addDiagnostic(int severity, const Token &token, const std::string &message, const std::string &code) {
            LSPDiagnostic d;
            d.severity = severity;
            d.code = code;
            d.message = message;
            d.source = "angara";
            d.range.startLine = token.line - 1;
            d.range.startChar = token.column - 1;
            d.range.endLine = token.line - 1;
            d.range.endChar = token.column - 1 + (int)token.lexeme.size();
            diagnostics.push_back(d);
        }

        void addDiagnostic(int severity, const Token &start, const Token &end,
                           const std::string &message, const std::string &code) {
            LSPDiagnostic d;
            d.severity = severity;
            d.code = code;
            d.message = message;
            d.source = "angara";
            d.range.startLine = start.line - 1;
            d.range.startChar = start.column - 1;
            d.range.endLine = end.line - 1;
            d.range.endChar = end.column - 1 + (int)end.lexeme.size();
            diagnostics.push_back(d);
        }
    };
}

// ── Document Analysis ──────────────────────────────────────────

void LSPServer::analyzeDocument(const std::string& uri) {
    auto it = m_documents.find(uri);
    if (it == m_documents.end()) return;
    auto& doc = it->second;

    AnalysisResult result;

    auto filename_ptr = std::make_shared<std::string>(doc.path);
    DiagnosticsCollector errorHandler(doc.source);

    // Stage 1: Lex
    Lexer lexer(doc.source, filename_ptr, errorHandler);
    auto tokens = lexer.scanTokens();

    // Collect lexer diagnostics
    result.diagnostics = errorHandler.diagnostics;

    if (!errorHandler.hadError()) {
        // Stage 2: Parse
        Parser parser(tokens, errorHandler);
        auto statements = parser.parseStmts();

        // Collect parse diagnostics
        result.diagnostics = errorHandler.diagnostics;

        if (!errorHandler.hadError()) {
            // Stage 3: Type check
            CompilerDriver driver;
            driver.set_check_only(true);
            driver.set_quiet(true);

            std::string native_mod_path = "/opt/angara/modules";
            if (fs::exists("build/modules")) {
                native_mod_path = fs::absolute("build/modules").string();
            }
            driver.set_paths("/opt/angara/src/modules", native_mod_path);

            std::string base_name = CompilerDriver::get_base_name(doc.path);
            TypeChecker typeChecker(driver, errorHandler, base_name);
            typeChecker.check(statements);

            // Collect all diagnostics (including type errors)
            result.diagnostics = errorHandler.diagnostics;

            // Collect completions from symbol table
            const auto& symbols = typeChecker.getSymbolTable();
            for (auto& scope : symbols.getScopes()) {
                for (auto& [name, sym] : scope) {
                    LSPCompletionItem item;
                    item.label = name;
                    if (sym->type) item.detail = sym->type->toString();
                    // Determine kind
                    if (sym->type && sym->type->kind == TypeKind::FUNCTION) {
                        item.kind = 3; // Function
                    } else if (sym->type && (sym->type->kind == TypeKind::CLASS ||
                              sym->type->kind == TypeKind::DATA ||
                              sym->type->kind == TypeKind::ENUM ||
                              sym->type->kind == TypeKind::TRAIT)) {
                        item.kind = 7; // Class
                    } else {
                        item.kind = 6; // Variable
                    }
                    result.completions.push_back(item);
                }
            }

            // Store expression types and variable resolutions for hover/definition
            // (We'll look these up on demand rather than pre-computing)
            const auto& exprTypes = typeChecker.getExpressionTypes();

            // Store completion items with their source locations for hover/def
            // For now, build a lookup map from source positions
            for (auto& [expr, type] : exprTypes) {
                if (auto* varExpr = dynamic_cast<const VarExpr*>(expr)) {
                    // Store for hover/def lookup
                    LSPCompletionItem item;
                    item.label = varExpr->name.lexeme;
                    item.detail = type->toString();
                    item.kind = (type->kind == TypeKind::FUNCTION) ? 3 : 6;
                    result.completions.push_back(item);
                }
            }
        }
    }

    m_analysis[uri] = std::move(result);
    publishDiagnostics(uri);
}

void LSPServer::publishDiagnostics(const std::string& uri) {
    auto it = m_analysis.find(uri);
    if (it == m_analysis.end()) return;

    JSON_ARR diagArr;
    for (auto& d : it->second.diagnostics) {
        JSON diag = JSON_OBJ{};
        diag["severity"] = d.severity;
        if (!d.code.empty()) diag["code"] = d.code;
        diag["source"] = d.source;
        diag["message"] = d.message;
        JSON range = JSON_OBJ{};
        JSON start = JSON_OBJ{};
        start["line"] = d.range.startLine;
        start["character"] = d.range.startChar;
        JSON end = JSON_OBJ{};
        end["line"] = d.range.endLine;
        end["character"] = d.range.endChar;
        range["start"] = start;
        range["end"] = end;
        diag["range"] = range;
        diagArr.push_back(diag);
    }

    JSON params = JSON_OBJ{};
    params["uri"] = uri;
    params["diagnostics"] = diagArr;
    notify("textDocument/publishDiagnostics", params);
}

// ── LSP Request Handlers ───────────────────────────────────────

JSON LSPServer::handleInitialize(const JSON& /*params*/) {
    JSON result = JSON_OBJ{};

    JSON caps = JSON_OBJ{};

    // Text document sync: full sync
    JSON sync = JSON_OBJ{};
    sync["openClose"] = true;
    sync["change"] = 1; // Full sync
    caps["textDocumentSync"] = sync;

    // Completion
    JSON compCaps = JSON_OBJ{};
    compCaps["completionItem"] = JSON_OBJ{{"snippetSupport", false}};
    caps["completionProvider"] = compCaps;

    // Hover
    caps["hoverProvider"] = true;

    // Go-to-definition
    caps["definitionProvider"] = true;

    result["capabilities"] = caps;
    result["serverInfo"] = JSON_OBJ{{"name", "angc-lsp"}, {"version", "3.1.0"}};
    return result;
}

void LSPServer::handleInitialized(const JSON&) {
    // Client acknowledged initialization - nothing to do
}

void LSPServer::handleDidOpen(const JSON& params) {
    auto& td = params["textDocument"];
    std::string uri = td["uri"].as_str();
    std::string text = td["text"].as_str();

    DocumentState doc;
    doc.path = uriToPath(uri);
    doc.source = text;
    if (td.has("version")) doc.version = td["version"].as_int();
    m_documents[uri] = std::move(doc);

    analyzeDocument(uri);
}

void LSPServer::handleDidChange(const JSON& params) {
    auto& td = params["textDocument"];
    std::string uri = td["uri"].as_str();

    auto& changes = params["contentChanges"];
    if (changes.size() > 0) {
        auto& doc = m_documents[uri];
        doc.source = changes[0]["text"].as_str();
        if (td.has("version")) doc.version = td["version"].as_int();
        analyzeDocument(uri);
    }
}

void LSPServer::handleDidClose(const JSON& params) {
    std::string uri = params["textDocument"]["uri"].as_str();
    m_documents.erase(uri);
    m_analysis.erase(uri);

    // Publish empty diagnostics to clear
    JSON params2 = JSON_OBJ{};
    params2["uri"] = uri;
    params2["diagnostics"] = JSON_ARR{};
    notify("textDocument/publishDiagnostics", params2);
}

JSON LSPServer::handleCompletion(const JSON& params) {
    std::string uri = params["textDocument"]["uri"].as_str();
    // int line = params["position"]["line"].as_int();
    // int character = params["position"]["character"].as_int();

    JSON_ARR items;
    auto it = m_analysis.find(uri);
    if (it != m_analysis.end()) {
        // Deduplicate by label
        std::set<std::string> seen;
        for (auto& c : it->second.completions) {
            if (seen.count(c.label)) continue;
            seen.insert(c.label);
            JSON item = JSON_OBJ{};
            item["label"] = c.label;
            item["kind"] = c.kind;
            if (!c.detail.empty()) item["detail"] = c.detail;
            items.push_back(item);
        }
    }
    return items;
}

JSON LSPServer::handleHover(const JSON& params) {
    std::string uri = params["textDocument"]["uri"].as_str();
    int line = params["position"]["line"].as_int();
    int character = params["position"]["character"].as_int();

    // Re-analyze to get expression types at position
    auto docIt = m_documents.find(uri);
    if (docIt == m_documents.end()) return JSON(nullptr);

    auto& doc = docIt->second;
    auto filename_ptr = std::make_shared<std::string>(doc.path);
    ErrorHandler errorHandler(doc.source);

    Lexer lexer(doc.source, filename_ptr, errorHandler);
    auto tokens = lexer.scanTokens();
    if (errorHandler.hadError()) return JSON(nullptr);

    Parser parser(tokens, errorHandler);
    auto statements = parser.parseStmts();
    if (errorHandler.hadError()) return JSON(nullptr);

    CompilerDriver driver;
    driver.set_check_only(true);
    driver.set_quiet(true);
    std::string native_mod_path = "/opt/angara/modules";
    if (fs::exists("build/modules")) {
        native_mod_path = fs::absolute("build/modules").string();
    }
    driver.set_paths("/opt/angara/src/modules", native_mod_path);

    std::string base_name = CompilerDriver::get_base_name(doc.path);
    TypeChecker typeChecker(driver, errorHandler, base_name);
    typeChecker.check(statements);
    if (errorHandler.hadError()) return JSON(nullptr);

    // Find expression at position (LSP is 0-based, Angara is 1-based)
    int target_line = line + 1;
    int target_col = character + 1;

    const auto& exprTypes = typeChecker.getExpressionTypes();
    const Expr* bestMatch = nullptr;
    std::shared_ptr<Type> bestType;
    int bestLen = 0;

    for (auto& [expr, type] : exprTypes) {
        // Walk the AST to find tokens at position
        // For now, check VarExpr specifically
        if (auto* var = dynamic_cast<const VarExpr*>(expr)) {
            if (var->name.line == target_line &&
                var->name.column <= target_col &&
                var->name.column + (int)var->name.lexeme.size() > target_col) {
                int len = (int)var->name.lexeme.size();
                if (len > bestLen) {
                    bestLen = len;
                    bestMatch = expr;
                    bestType = type;
                }
            }
        }
    }

    if (!bestMatch || !bestType) return JSON(nullptr);

    JSON result = JSON_OBJ{};
    JSON contents = JSON_OBJ{};
    contents["kind"] = "plaintext";
    contents["value"] = bestType->toString();
    result["contents"] = contents;
    return result;
}

JSON LSPServer::handleDefinition(const JSON& params) {
    std::string uri = params["textDocument"]["uri"].as_str();
    int line = params["position"]["line"].as_int();
    int character = params["position"]["character"].as_int();

    auto docIt = m_documents.find(uri);
    if (docIt == m_documents.end()) return JSON(nullptr);

    auto& doc = docIt->second;
    auto filename_ptr = std::make_shared<std::string>(doc.path);
    ErrorHandler errorHandler(doc.source);

    Lexer lexer(doc.source, filename_ptr, errorHandler);
    auto tokens = lexer.scanTokens();
    if (errorHandler.hadError()) return JSON(nullptr);

    Parser parser(tokens, errorHandler);
    auto statements = parser.parseStmts();
    if (errorHandler.hadError()) return JSON(nullptr);

    CompilerDriver driver;
    driver.set_check_only(true);
    driver.set_quiet(true);
    std::string native_mod_path = "/opt/angara/modules";
    if (fs::exists("build/modules")) {
        native_mod_path = fs::absolute("build/modules").string();
    }
    driver.set_paths("/opt/angara/src/modules", native_mod_path);

    std::string base_name = CompilerDriver::get_base_name(doc.path);
    TypeChecker typeChecker(driver, errorHandler, base_name);
    typeChecker.check(statements);

    // Find variable at position and resolve to its declaration
    int target_line = line + 1;
    int target_col = character + 1;

    const auto& varResolutions = typeChecker.getVariableResolutions();
    for (auto& [varExpr, symbol] : varResolutions) {
        if (varExpr->name.line == target_line &&
            varExpr->name.column <= target_col &&
            varExpr->name.column + (int)varExpr->name.lexeme.size() > target_col) {
            // Found the variable - resolve to declaration
            auto& declToken = symbol->declaration_token;
            std::string declFile = declToken.file ? *declToken.file : doc.path;

            JSON result = JSON_OBJ{};
            result["uri"] = pathToUri(declFile);
            JSON range = JSON_OBJ{};
            JSON start = JSON_OBJ{};
            start["line"] = declToken.line - 1;
            start["character"] = declToken.column - 1;
            JSON end = JSON_OBJ{};
            end["line"] = declToken.line - 1;
            end["character"] = declToken.column - 1 + (int)declToken.lexeme.size();
            range["start"] = start;
            range["end"] = end;
            result["range"] = range;
            return result;
        }
    }

    return JSON(nullptr);
}

JSON LSPServer::handleShutdown() {
    m_shutdown = true;
    return JSON(nullptr);
}

void LSPServer::handleExit() {
    exit(m_shutdown ? 0 : 1);
}

// ── Main Loop ──────────────────────────────────────────────────

int LSPServer::run() {
    // Use stdin/stdout in binary mode on Windows (no-op on Unix)
    while (true) {
        std::string body = readMessage();
        if (body.empty()) break;

        JSON msg;
        try {
            msg = JSON::parse(body);
        } catch (const std::exception& e) {
            continue;
        }

        if (!msg.has("method")) continue;
        std::string method = msg["method"].as_str();
        bool has_id = msg.has("id");
        JSON id = has_id ? msg["id"] : JSON(nullptr);

        if (method == "initialize") {
            sendResponse(id, handleInitialize(msg["params"]));
        } else if (method == "initialized") {
            handleInitialized(msg["params"]);
        } else if (method == "textDocument/didOpen") {
            handleDidOpen(msg["params"]);
        } else if (method == "textDocument/didChange") {
            handleDidChange(msg["params"]);
        } else if (method == "textDocument/didClose") {
            handleDidClose(msg["params"]);
        } else if (method == "textDocument/completion") {
            sendResponse(id, handleCompletion(msg["params"]));
        } else if (method == "textDocument/hover") {
            sendResponse(id, handleHover(msg["params"]));
        } else if (method == "textDocument/definition") {
            sendResponse(id, handleDefinition(msg["params"]));
        } else if (method == "shutdown") {
            sendResponse(id, handleShutdown());
        } else if (method == "exit") {
            handleExit();
        }
    }
    return 0;
}

} // namespace angara
