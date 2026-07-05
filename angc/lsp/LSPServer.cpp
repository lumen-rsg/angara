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
#include "Chaperone.h"

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

        void note(const Token &token, const std::string &message) override {
            ErrorHandler::note(token, message);
            // Attach as related information to the last diagnostic
            if (!diagnostics.empty()) {
                auto& last = diagnostics.back();
                LSPDiagnosticRelated rel;
                rel.message = message;
                std::string file = token.file ? *token.file : "";
                rel.uri = "file://" + file;
                rel.range.startLine = token.line - 1;
                rel.range.startChar = token.column - 1;
                rel.range.endLine = token.line - 1;
                rel.range.endChar = token.column - 1 + (int)token.lexeme.size();
                last.related.push_back(std::move(rel));
            }
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
            if (!m_workspace_root.empty()) {
                auto p = fs::path(m_workspace_root) / "build/modules";
                if (fs::exists(p)) {
                    native_mod_path = fs::absolute(p).string();
                }
            } else if (fs::exists("build/modules")) {
                native_mod_path = fs::absolute("build/modules").string();
            }
            driver.set_paths("/opt/angara/src/modules", native_mod_path);

            std::string base_name = CompilerDriver::get_base_name(doc.path);
            TypeChecker typeChecker(driver, errorHandler, base_name);
            typeChecker.check(statements);

            // Stage 4: Chaperone — compile-time memory verification. Runs only
            // if type-checking succeeded (the pass needs a typed AST). Unlike the
            // compiler driver, the LSP does NOT halt on Chaperone errors — they
            // are published as diagnostics (squiggles) so hover/completion still
            // work while the programmer fixes the memory bug. (Stage 8.)
            if (!errorHandler.hadError()) {
                Chaperone::run(statements, typeChecker, errorHandler);
            }

            // Collect all diagnostics (type errors + Chaperone E5xx/W510)
            result.diagnostics = errorHandler.diagnostics;

            // Build hover/definition/completion cache from type checker results.
            // Runs regardless of Chaperone errors so the editor stays usable.
            // Cast to const shared_ptr for storage
            std::vector<std::shared_ptr<const Stmt>> constStmts;
            for (auto& s : statements) constStmts.push_back(s);
            buildSymbolCache(result, typeChecker, constStmts, doc.path);
            result.topLevelStmts = std::move(constStmts);
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
        // Attach related information (e.g. "declared here" notes)
        if (!d.related.empty()) {
            JSON_ARR relatedArr;
            for (auto& r : d.related) {
                JSON rel = JSON_OBJ{};
                rel["message"] = r.message;
                JSON loc = JSON_OBJ{};
                loc["uri"] = r.uri;
                JSON relRange = JSON_OBJ{};
                JSON relStart = JSON_OBJ{};
                relStart["line"] = r.range.startLine;
                relStart["character"] = r.range.startChar;
                JSON relEnd = JSON_OBJ{};
                relEnd["line"] = r.range.endLine;
                relEnd["character"] = r.range.endChar;
                relRange["start"] = relStart;
                relRange["end"] = relEnd;
                loc["range"] = relRange;
                rel["location"] = loc;
                relatedArr.push_back(rel);
            }
            diag["relatedInformation"] = relatedArr;
        }
        diagArr.push_back(diag);
    }

    JSON params = JSON_OBJ{};
    params["uri"] = uri;
    params["diagnostics"] = diagArr;
    notify("textDocument/publishDiagnostics", params);
}

// ── Symbol Cache & Word Extraction ──────────────────────────────

void LSPServer::buildSymbolCache(AnalysisResult& result, TypeChecker& typeChecker,
                                 const std::vector<std::shared_ptr<const Stmt>>& stmts,
                                 const std::string& docPath) {
    const auto& exprTypes = typeChecker.getExpressionTypes();
    const auto& varResolutions = typeChecker.getVariableResolutions();
    const auto& symbols = typeChecker.getSymbolTable();

    // Build position-indexed entries from expression types
    for (auto& [expr, type] : exprTypes) {
        if (!type || type->kind == TypeKind::ERROR) continue;

        if (auto* var = dynamic_cast<const VarExpr*>(expr)) {
            SymbolRef ref;
            ref.name = var->name.lexeme;
            ref.typeString = type->toString();
            ref.line = var->name.line - 1;
            ref.startCol = var->name.column - 1;
            ref.endCol = ref.startCol + (int)var->name.lexeme.size();

            auto resIt = varResolutions.find(var);
            if (resIt != varResolutions.end()) {
                auto& decl = resIt->second->declaration_token;
                ref.declFile = decl.file ? *decl.file : docPath;
                ref.declLine = decl.line - 1;
                ref.declCol = decl.column - 1;
            }
            result.symbols.push_back(std::move(ref));
        }
        else if (auto* get = dynamic_cast<const GetExpr*>(expr)) {
            SymbolRef ref;
            ref.name = get->name.lexeme;
            ref.typeString = type->toString();
            ref.line = get->name.line - 1;
            ref.startCol = get->name.column - 1;
            ref.endCol = ref.startCol + (int)get->name.lexeme.size();
            result.symbols.push_back(std::move(ref));
        }
        // Collect call sites for signature help
        else if (auto* call = dynamic_cast<const CallExpr*>(expr)) {
            // Look up the callee's type (which is the function type)
            auto calleeIt = exprTypes.find(call->callee.get());
            if (calleeIt != exprTypes.end() && calleeIt->second &&
                calleeIt->second->kind == TypeKind::FUNCTION) {
                auto funcType = std::dynamic_pointer_cast<FunctionType>(calleeIt->second);
                CallSiteInfo csi;
                csi.signature = funcType->toString();
                csi.openParenLine = call->paren.line - 1;
                csi.openParenCol = call->paren.column - 1;
                // Try to get callee name
                if (auto* varCallee = dynamic_cast<const VarExpr*>(call->callee.get())) {
                    csi.calleeName = varCallee->name.lexeme;
                } else if (auto* getCallee = dynamic_cast<const GetExpr*>(call->callee.get())) {
                    csi.calleeName = getCallee->name.lexeme;
                }
                result.callSites.push_back(std::move(csi));
            }
        }
    }

    // Build name-indexed symbol table and declaration-position entries
    for (auto& scope : symbols.getScopes()) {
        for (auto& [name, sym] : scope) {
            if (!sym || !sym->type || sym->type->kind == TypeKind::ERROR) continue;

            // Symbol table entry (inner scopes override outer)
            SymbolTableEntry entry;
            entry.typeString = sym->type->toString();
            entry.declFile = sym->declaration_token.file ? *sym->declaration_token.file : docPath;
            entry.declLine = sym->declaration_token.line - 1;
            entry.declCol = sym->declaration_token.column - 1;
            result.symbolTable[name] = std::move(entry);

            // Completion item
            LSPCompletionItem item;
            item.label = name;
            item.detail = sym->type->toString();
            if (sym->type->kind == TypeKind::FUNCTION) {
                item.kind = 3; // Function
            } else if (sym->type->kind == TypeKind::CLASS ||
                       sym->type->kind == TypeKind::DATA ||
                       sym->type->kind == TypeKind::ENUM ||
                       sym->type->kind == TypeKind::TRAIT ||
                       sym->type->kind == TypeKind::CONTRACT) {
                item.kind = 7; // Class
            } else if (sym->type->kind == TypeKind::MODULE) {
                item.kind = 9; // Module
            } else {
                item.kind = 6; // Variable
            }
            result.completions.push_back(std::move(item));
        }
    }

    // Build document symbols from top-level statements
    for (auto& stmt : stmts) {
        if (auto* fn = dynamic_cast<const FuncStmt*>(stmt.get())) {
            DocumentSymbolInfo dsi;
            dsi.name = fn->name.lexeme;
            dsi.kind = 12; // Function
            dsi.startLine = fn->name.line - 1;
            dsi.startCol = fn->name.column - 1;
            dsi.endLine = fn->name.line - 1;
            dsi.endCol = dsi.startCol + (int)fn->name.lexeme.size();
            result.documentSymbols.push_back(std::move(dsi));
        } else if (auto* cls = dynamic_cast<const ClassStmt*>(stmt.get())) {
            DocumentSymbolInfo dsi;
            dsi.name = cls->name.lexeme;
            dsi.kind = 5; // Class
            dsi.startLine = cls->name.line - 1;
            dsi.startCol = cls->name.column - 1;
            dsi.endLine = cls->name.line - 1;
            dsi.endCol = dsi.startCol + (int)cls->name.lexeme.size();
            result.documentSymbols.push_back(std::move(dsi));
        } else if (auto* data = dynamic_cast<const DataStmt*>(stmt.get())) {
            DocumentSymbolInfo dsi;
            dsi.name = data->name.lexeme;
            dsi.kind = 23; // Struct
            dsi.startLine = data->name.line - 1;
            dsi.startCol = data->name.column - 1;
            dsi.endLine = data->name.line - 1;
            dsi.endCol = dsi.startCol + (int)data->name.lexeme.size();
            result.documentSymbols.push_back(std::move(dsi));
        } else if (auto* en = dynamic_cast<const EnumStmt*>(stmt.get())) {
            DocumentSymbolInfo dsi;
            dsi.name = en->name.lexeme;
            dsi.kind = 10; // Enum
            dsi.startLine = en->name.line - 1;
            dsi.startCol = en->name.column - 1;
            dsi.endLine = en->name.line - 1;
            dsi.endCol = dsi.startCol + (int)en->name.lexeme.size();
            result.documentSymbols.push_back(std::move(dsi));
        } else if (auto* tr = dynamic_cast<const TraitStmt*>(stmt.get())) {
            DocumentSymbolInfo dsi;
            dsi.name = tr->name.lexeme;
            dsi.kind = 22; // Interface
            dsi.startLine = tr->name.line - 1;
            dsi.startCol = tr->name.column - 1;
            dsi.endLine = tr->name.line - 1;
            dsi.endCol = dsi.startCol + (int)tr->name.lexeme.size();
            result.documentSymbols.push_back(std::move(dsi));
        } else if (auto* ct = dynamic_cast<const ContractStmt*>(stmt.get())) {
            DocumentSymbolInfo dsi;
            dsi.name = ct->name.lexeme;
            dsi.kind = 22; // Interface
            dsi.startLine = ct->name.line - 1;
            dsi.startCol = ct->name.column - 1;
            dsi.endLine = ct->name.line - 1;
            dsi.endCol = dsi.startCol + (int)ct->name.lexeme.size();
            result.documentSymbols.push_back(std::move(dsi));
        } else if (auto* alias = dynamic_cast<const TypeAliasStmt*>(stmt.get())) {
            DocumentSymbolInfo dsi;
            dsi.name = alias->name.lexeme;
            dsi.kind = 23; // Struct (same as data)
            dsi.startLine = alias->name.line - 1;
            dsi.startCol = alias->name.column - 1;
            dsi.endLine = alias->name.line - 1;
            dsi.endCol = dsi.startCol + (int)alias->name.lexeme.size();
            result.documentSymbols.push_back(std::move(dsi));
        }
    }
}

std::string LSPServer::extractWordAt(const std::string& source, int line, int col) {
    // Find the start of the requested line
    int currentLine = 0;
    size_t lineStart = 0;
    for (size_t i = 0; i < source.size(); ++i) {
        if (currentLine == line) {
            lineStart = i;
            break;
        }
        if (source[i] == '\n') currentLine++;
    }
    if (currentLine < line) return "";

    // Find end of line
    size_t lineEnd = source.find('\n', lineStart);
    if (lineEnd == std::string::npos) lineEnd = source.size();

    if (col < 0 || col >= (int)(lineEnd - lineStart)) return "";

    auto isIdentChar = [](char c) {
        return isalnum(static_cast<unsigned char>(c)) || c == '_';
    };

    int start = col;
    while (start > 0 && isIdentChar(source[lineStart + start - 1])) start--;

    int end = col;
    while (end < (int)(lineEnd - lineStart) && isIdentChar(source[lineStart + end])) end++;

    if (end <= start) return "";
    return source.substr(lineStart + start, end - start);
}

// ── LSP Request Handlers ───────────────────────────────────────

JSON LSPServer::handleInitialize(const JSON& params) {
    // Capture workspace root
    if (params.has("rootUri") && !params["rootUri"].is_null()) {
        m_workspace_root = uriToPath(params["rootUri"].as_str());
    } else if (params.has("rootPath") && !params["rootPath"].is_null()) {
        m_workspace_root = params["rootPath"].as_str();
    }

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

    // Signature help
    JSON sigCaps = JSON_OBJ{};
    sigCaps["triggerCharacters"] = JSON_ARR{"(", ","};
    caps["signatureHelpProvider"] = sigCaps;

    // Document symbols
    caps["documentSymbolProvider"] = true;

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

    auto analysisIt = m_analysis.find(uri);
    if (analysisIt == m_analysis.end()) return JSON(nullptr);
    auto& analysis = analysisIt->second;

    // 1. Try positional match from expression types
    const SymbolRef* best = nullptr;
    int bestLen = 0;
    for (auto& ref : analysis.symbols) {
        if (ref.line == line && ref.startCol <= character && ref.endCol > character) {
            if ((ref.endCol - ref.startCol) > bestLen) {
                bestLen = ref.endCol - ref.startCol;
                best = &ref;
            }
        }
    }

    // 2. Fallback: look up the word at cursor in the symbol table
    if (!best) {
        auto docIt = m_documents.find(uri);
        if (docIt != m_documents.end()) {
            std::string word = extractWordAt(docIt->second.source, line, character);
            if (!word.empty()) {
                auto symIt = analysis.symbolTable.find(word);
                if (symIt != analysis.symbolTable.end() && !symIt->second.typeString.empty()) {
                    JSON result = JSON_OBJ{};
                    JSON contents = JSON_OBJ{};
                    contents["kind"] = "plaintext";
                    contents["value"] = symIt->second.typeString;
                    result["contents"] = contents;
                    return result;
                }
            }
        }
    }

    if (!best || best->typeString.empty()) return JSON(nullptr);

    JSON result = JSON_OBJ{};
    JSON contents = JSON_OBJ{};
    contents["kind"] = "plaintext";
    contents["value"] = best->typeString;
    result["contents"] = contents;
    return result;
}

JSON LSPServer::handleDefinition(const JSON& params) {
    std::string uri = params["textDocument"]["uri"].as_str();
    int line = params["position"]["line"].as_int();
    int character = params["position"]["character"].as_int();

    auto analysisIt = m_analysis.find(uri);
    if (analysisIt == m_analysis.end()) return JSON(nullptr);
    auto& analysis = analysisIt->second;

    // 1. Try positional match from expression types
    const SymbolRef* best = nullptr;
    int bestLen = 0;
    for (auto& ref : analysis.symbols) {
        if (ref.line == line && ref.startCol <= character && ref.endCol > character) {
            if ((ref.endCol - ref.startCol) > bestLen) {
                bestLen = ref.endCol - ref.startCol;
                best = &ref;
            }
        }
    }

    if (best && !best->declFile.empty()) {
        JSON result = JSON_OBJ{};
        result["uri"] = pathToUri(best->declFile);
        JSON range = JSON_OBJ{};
        JSON start = JSON_OBJ{};
        start["line"] = best->declLine;
        start["character"] = best->declCol;
        JSON end = JSON_OBJ{};
        end["line"] = best->declLine;
        end["character"] = best->declCol + (int)best->name.size();
        range["start"] = start;
        range["end"] = end;
        result["range"] = range;
        return result;
    }

    // 2. Fallback: word-at-cursor in symbol table
    auto docIt = m_documents.find(uri);
    if (docIt != m_documents.end()) {
        std::string word = extractWordAt(docIt->second.source, line, character);
        if (!word.empty()) {
            auto symIt = analysis.symbolTable.find(word);
            if (symIt != analysis.symbolTable.end() && !symIt->second.declFile.empty()) {
                JSON result = JSON_OBJ{};
                result["uri"] = pathToUri(symIt->second.declFile);
                JSON range = JSON_OBJ{};
                JSON start = JSON_OBJ{};
                start["line"] = symIt->second.declLine;
                start["character"] = symIt->second.declCol;
                JSON end = JSON_OBJ{};
                end["line"] = symIt->second.declLine;
                end["character"] = symIt->second.declCol + (int)word.size();
                range["start"] = start;
                range["end"] = end;
                result["range"] = range;
                return result;
            }
        }
    }

    return JSON(nullptr);
}

JSON LSPServer::handleSignatureHelp(const JSON& params) {
    std::string uri = params["textDocument"]["uri"].as_str();
    int line = params["position"]["line"].as_int();
    int character = params["position"]["character"].as_int();

    auto analysisIt = m_analysis.find(uri);
    if (analysisIt == m_analysis.end()) return JSON(nullptr);
    auto& analysis = analysisIt->second;

    // Find the innermost call site enclosing the cursor
    const CallSiteInfo* best = nullptr;
    for (auto& cs : analysis.callSites) {
        // The cursor must be after the '(' on the same line or on subsequent lines
        if (cs.openParenLine < line ||
            (cs.openParenLine == line && cs.openParenCol < character)) {
            if (!best || (cs.openParenLine > best->openParenLine ||
                (cs.openParenLine == best->openParenLine && cs.openParenCol > best->openParenCol))) {
                best = &cs;
            }
        }
    }

    if (!best) return JSON(nullptr);

    // Count commas between '(' and cursor to determine active parameter
    auto docIt = m_documents.find(uri);
    int activeParam = 0;
    if (docIt != m_documents.end()) {
        const std::string& src = docIt->second.source;
        int currentLine = 0;
        size_t lineStart = 0;
        for (size_t i = 0; i < src.size(); ++i) {
            if (currentLine == best->openParenLine) { lineStart = i; break; }
            if (src[i] == '\n') currentLine++;
        }
        // Find the '(' position in source
        size_t parenPos = lineStart + best->openParenCol;
        // Count commas between '(' and cursor position
        size_t cursorPos = 0;
        int cl = 0;
        for (size_t i = 0; i < src.size(); ++i) {
            if (cl == line && (int)(i - lineStart) == character) { cursorPos = i; break; }
            if (src[i] == '\n') cl++;
        }
        if (cursorPos == 0 && cl <= line) cursorPos = src.size();
        int depth = 1;
        for (size_t i = parenPos + 1; i < cursorPos && i < src.size(); ++i) {
            if (src[i] == '(') depth++;
            else if (src[i] == ')') { depth--; if (depth == 0) break; }
            else if (src[i] == ',' && depth == 1) activeParam++;
        }
    }

    JSON sigInfo = JSON_OBJ{};
    JSON_ARR signatures;
    JSON sig = JSON_OBJ{};
    sig["label"] = best->calleeName.empty() ? best->signature : best->calleeName + best->signature.substr(8); // strip "function" prefix
    sig["activeParameter"] = activeParam;
    signatures.push_back(sig);
    sigInfo["signatures"] = signatures;
    return sigInfo;
}

JSON LSPServer::handleDocumentSymbol(const JSON& params) {
    std::string uri = params["textDocument"]["uri"].as_str();

    auto analysisIt = m_analysis.find(uri);
    if (analysisIt == m_analysis.end()) return JSON_ARR{};

    JSON_ARR result;
    for (auto& ds : analysisIt->second.documentSymbols) {
        JSON sym = JSON_OBJ{};
        sym["name"] = ds.name;
        sym["kind"] = ds.kind;
        if (!ds.detail.empty()) sym["detail"] = ds.detail;
        JSON range = JSON_OBJ{};
        JSON start = JSON_OBJ{};
        start["line"] = ds.startLine;
        start["character"] = ds.startCol;
        JSON end = JSON_OBJ{};
        end["line"] = ds.endLine;
        end["character"] = ds.endCol;
        range["start"] = start;
        range["end"] = end;
        sym["range"] = range;
        sym["selectionRange"] = range;
        result.push_back(sym);
    }
    return result;
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
        } else if (method == "textDocument/signatureHelp") {
            sendResponse(id, handleSignatureHelp(msg["params"]));
        } else if (method == "textDocument/documentSymbol") {
            sendResponse(id, handleDocumentSymbol(msg["params"]));
        } else if (method == "shutdown") {
            sendResponse(id, handleShutdown());
        } else if (method == "exit") {
            handleExit();
        }
    }
    return 0;
}

} // namespace angara
