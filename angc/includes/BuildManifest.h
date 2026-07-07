#pragma once

#include <string>
#include <map>
#include <set>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdint>
#include <chrono>
#include <iostream>

namespace angara {

/// Manages the incremental compilation cache: records per-module source
/// mtimes, dependency mtimes, and object-file paths so that unchanged
/// modules can skip LLVM codegen on rebuild.
///
/// The manifest is stored as JSON at `<build_dir>/manifest.json`.
class BuildManifest {
public:
    struct ModuleEntry {
        std::string name;
        int64_t src_mtime = 0;          // source file last-write time
        uintmax_t src_size = 0;         // L15: source file size for additional freshness check
        std::map<std::string, int64_t> dep_mtimes;  // dep path → mtime
        std::string obj_file;           // cached .o file path
    };

    /// Loads the manifest from `path`.  If the file doesn't exist or is
    /// unparseable, starts with an empty manifest.
    void load(const std::string& path) {
        m_path = path;
        m_entries.clear();

        std::ifstream in(path);
        if (!in.is_open()) return;

        std::stringstream buffer;
        buffer << in.rdbuf();
        std::string content = buffer.str();

        parse(content);
    }

    /// Saves the manifest to the same path used for load().
    void save() {
        if (m_path.empty()) return;

        // Ensure parent directory exists.
        std::filesystem::path p(m_path);
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);

        std::ofstream out(m_path);
        if (!out.is_open()) return;
        out << serialize();
    }

    /// Returns true if the module is "clean" — its source mtime and all
    /// dependency mtimes match the manifest.  A module not in the manifest
    /// is always dirty.
    bool isClean(const std::string& module_path,
                 int64_t current_src_mtime,
                 uintmax_t current_src_size,
                 const std::map<std::string, int64_t>& current_dep_mtimes) const {
        auto it = m_entries.find(module_path);
        if (it == m_entries.end()) return false;

        const auto& entry = it->second;
        // L15: check both mtime and file size to catch timestamp-granularity issues
        if (entry.src_mtime != current_src_mtime) return false;
        if (entry.src_size != current_src_size) return false;

        // Check that all dependency mtimes match.
        if (entry.dep_mtimes.size() != current_dep_mtimes.size()) return false;
        for (const auto& [dep_path, mtime] : current_dep_mtimes) {
            auto dit = entry.dep_mtimes.find(dep_path);
            if (dit == entry.dep_mtimes.end()) return false;
            if (dit->second != mtime) return false;
        }

        // Check that the cached .o file still exists.
        if (!std::filesystem::exists(entry.obj_file)) return false;

        return true;
    }

    /// Retrieves the cached object file path for a module.
    /// Returns empty string if the module is not in the manifest.
    std::string getObjFile(const std::string& module_path) const {
        auto it = m_entries.find(module_path);
        return (it != m_entries.end()) ? it->second.obj_file : std::string{};
    }

    /// Adds or updates an entry for a freshly-compiled module.
    void addEntry(const std::string& module_path,
                  const std::string& name,
                  int64_t src_mtime,
                  uintmax_t src_size,
                  const std::map<std::string, int64_t>& dep_mtimes,
                  const std::string& obj_file) {
        ModuleEntry entry;
        entry.name = name;
        entry.src_mtime = src_mtime;
        entry.src_size = src_size;  // L15
        entry.dep_mtimes = dep_mtimes;
        entry.obj_file = obj_file;
        m_entries[module_path] = std::move(entry);
    }

    /// Removes an entry (e.g., when a module's source was deleted).
    void removeEntry(const std::string& module_path) {
        m_entries.erase(module_path);
    }

    /// Returns all tracked module paths.
    std::set<std::string> allModules() const {
        std::set<std::string> result;
        for (const auto& [path, _] : m_entries) result.insert(path);
        return result;
    }

    /// Clears all entries.
    void clear() { m_entries.clear(); }

private:
    // ── Minimal JSON serializer ──────────────────────────────────────────
    std::string serialize() const {
        std::stringstream ss;
        ss << "{\n  \"version\": 1,\n  \"modules\": {\n";
        bool first_mod = true;
        for (const auto& [path, entry] : m_entries) {
            if (!first_mod) ss << ",\n";
            first_mod = false;
            ss << "    " << jsonString(path) << ": {\n";
            ss << "      \"name\": " << jsonString(entry.name) << ",\n";
            ss << "      \"src_mtime\": " << entry.src_mtime << ",\n";
            ss << "      \"src_size\": " << entry.src_size << ",\n";  // L15
            ss << "      \"obj_file\": " << jsonString(entry.obj_file) << ",\n";
            ss << "      \"deps\": {\n";
            bool first_dep = true;
            for (const auto& [dep_path, mtime] : entry.dep_mtimes) {
                if (!first_dep) ss << ",\n";
                first_dep = false;
                ss << "        " << jsonString(dep_path) << ": " << mtime;
            }
            ss << "\n      }\n";
            ss << "    }";
        }
        ss << "\n  }\n}\n";
        return ss.str();
    }

    static std::string jsonString(const std::string& s) {
        std::stringstream ss;
        ss << '"';
        for (char c : s) {
            switch (c) {
                case '"':  ss << "\\\""; break;
                case '\\': ss << "\\\\"; break;
                case '\n': ss << "\\n"; break;
                case '\t': ss << "\\t"; break;
                default:   ss << c;
            }
        }
        ss << '"';
        return ss.str();
    }

    // ── Minimal JSON parser ──────────────────────────────────────────────
    void parse(const std::string& content) {
        m_pos = 0;
        m_content = &content;
        skipWhitespace();

        if (peek() != '{') return;  // not JSON
        parseObject([this](const std::string& key) {
            if (key == "modules") {
                parseObject([this](const std::string& mod_path) {
                    ModuleEntry entry;
                    parseObject([&](const std::string& field) {
                        if (field == "name")
                            entry.name = parseString();
                        else if (field == "src_mtime")
                            entry.src_mtime = parseNumber();
                        else if (field == "src_size")       // L15
                            entry.src_size = static_cast<uintmax_t>(parseNumber());
                        else if (field == "obj_file")
                            entry.obj_file = parseString();
                        else if (field == "deps") {
                            parseObject([&](const std::string& dep_path) {
                                int64_t mtime = parseNumber();
                                entry.dep_mtimes[dep_path] = mtime;
                            });
                        } else {
                            skipValue();
                        }
                    });
                    m_entries[mod_path] = std::move(entry);
                });
            } else {
                skipValue();
            }
        });
    }

    // Parser helpers.
    char peek() const {
        return m_pos < m_content->size() ? (*m_content)[m_pos] : '\0';
    }

    char advance() {
        return m_pos < m_content->size() ? (*m_content)[m_pos++] : '\0';
    }

    void skipWhitespace() {
        while (m_pos < m_content->size()) {
            char c = (*m_content)[m_pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++m_pos;
            } else {
                break;
            }
        }
    }

    std::string parseString() {
        skipWhitespace();
        if (advance() != '"') return {};
        std::string result;
        while (m_pos < m_content->size()) {
            char c = advance();
            if (c == '"') break;
            if (c == '\\' && m_pos < m_content->size()) {
                char next = advance();
                switch (next) {
                    case '"':  result += '"'; break;
                    case '\\': result += '\\'; break;
                    case 'n':  result += '\n'; break;
                    case 't':  result += '\t'; break;
                    default:   result += next;
                }
            } else {
                result += c;
            }
        }
        return result;
    }

    int64_t parseNumber() {
        skipWhitespace();
        std::string num;
        if (peek() == '-') num += advance();
        while (m_pos < m_content->size() && isdigit(peek())) {
            num += advance();
        }
        if (num.empty()) return 0;
        return std::stoll(num);
    }

    void parseObject(std::function<void(const std::string& key)> callback) {
        skipWhitespace();
        if (advance() != '{') return;
        skipWhitespace();
        if (peek() == '}') { advance(); return; }

        while (true) {
            skipWhitespace();
            std::string key = parseString();
            skipWhitespace();
            if (advance() != ':') break;  // malformed
            skipWhitespace();
            callback(key);
            skipWhitespace();
            char c = advance();
            if (c == '}') break;
            if (c != ',') break;  // malformed
        }
    }

    void skipValue() {
        skipWhitespace();
        char c = peek();
        if (c == '"') {
            parseString();
        } else if (c == '{') {
            int depth = 1;
            advance();
            while (depth > 0 && m_pos < m_content->size()) {
                char ch = advance();
                if (ch == '{') depth++;
                else if (ch == '}') depth--;
                else if (ch == '"') parseString();
            }
        } else if (c == '[') {
            int depth = 1;
            advance();
            while (depth > 0 && m_pos < m_content->size()) {
                char ch = advance();
                if (ch == '[') depth++;
                else if (ch == ']') depth--;
                else if (ch == '"') parseString();
            }
        } else {
            // Number, boolean, or null — consume until next structural char.
            while (m_pos < m_content->size()) {
                char ch = peek();
                if (ch == ',' || ch == '}' || ch == ']' ||
                    ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') break;
                advance();
            }
        }
    }

    std::string m_path;
    std::map<std::string, ModuleEntry> m_entries;
    const std::string* m_content = nullptr;
    size_t m_pos = 0;
};

} // namespace angara
