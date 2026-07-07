#include "../../includes/Lockfile.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <filesystem>

namespace angara {

// ── JSON helpers ────────────────────────────────────────────────────────

std::string Lockfile::escape_json(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:   out += c; break;
        }
    }
    return out;
}

// Simple recursive-descent JSON parser (only what we need).
namespace {

class JsonParser {
public:
    explicit JsonParser(const std::string& src) : m_src(src), m_pos(0) {}

    bool parse_object(std::map<std::string, std::string>& fields) {
        skip_ws();
        if (!expect('{')) return false;
        skip_ws();
        if (peek() == '}') { advance(); return true; }
        while (true) {
            skip_ws();
            std::string key = parse_string();
            if (key.empty() && peek() != '"') return false;
            skip_ws();
            if (!expect(':')) return false;
            skip_ws();
            std::string val = parse_value();
            fields[key] = val;
            skip_ws();
            char c = peek();
            if (c == '}') { advance(); return true; }
            if (c != ',') return false;
            advance(); // skip ','
        }
    }

    bool parse_object_nested(std::map<std::string, std::map<std::string, std::string>>& result) {
        skip_ws();
        if (!expect('{')) return false;
        skip_ws();
        if (peek() == '}') { advance(); return true; }
        while (true) {
            skip_ws();
            std::string key = parse_string();
            if (key.empty()) return false;
            skip_ws();
            if (!expect(':')) return false;
            skip_ws();
            std::map<std::string, std::string> inner;
            if (!parse_object(inner)) return false;
            result[key] = std::move(inner);
            skip_ws();
            char c = peek();
            if (c == '}') { advance(); return true; }
            if (c != ',') return false;
            advance();
        }
    }

    bool parse_array(std::vector<std::string>& result) {
        skip_ws();
        if (!expect('[')) return false;
        skip_ws();
        if (peek() == ']') { advance(); return true; }
        while (true) {
            skip_ws();
            result.push_back(parse_string());
            skip_ws();
            char c = peek();
            if (c == ']') { advance(); return true; }
            if (c != ',') return false;
            advance();
        }
    }

    // H12: Find the position of a top-level key's value in a JSON object,
    // properly skipping string literals so that the key text inside a string
    // value does not cause a false match. Returns the position of the value's
    // first character, or npos if the key is not found.
    size_t find_value_pos(const std::string& key) {
        // We expect to start at a '{'. Skip it and scan for "key":
        skip_ws();
        if (!expect('{')) return std::string::npos;
        std::string search = "\"" + key + "\"";
        while (m_pos < m_src.size()) {
            skip_ws();
            char c = peek();
            if (c == '}') return std::string::npos; // end of object, key not found
            if (c == ',') { advance(); continue; }

            // We're at a key (must be a string). Remember position, parse it.
            if (c == '"') {
                size_t key_start = m_pos;
                std::string parsed_key = parse_string();
                if (parsed_key.empty()) return std::string::npos;
                skip_ws();
                if (!expect(':')) return std::string::npos;
                if (parsed_key == key) {
                    skip_ws();
                    return m_pos; // position of value
                }
                // Not our key — skip the value, handling nested objects/arrays
                skip_value();
                continue;
            }
            return std::string::npos; // unexpected
        }
        return std::string::npos;
    }

private:
    // Skip a JSON value (string, number, bool, null, object, or array).
    void skip_value() {
        skip_ws();
        char c = peek();
        if (c == '"') {
            parse_string(); // skip string
        } else if (c == '{') {
            advance();
            int depth = 1;
            while (m_pos < m_src.size() && depth > 0) {
                char sc = m_src[m_pos++];
                if (sc == '"') {
                    // skip string inside the nested object
                    while (m_pos < m_src.size()) {
                        char st = m_src[m_pos++];
                        if (st == '"') break;
                        if (st == '\\' && m_pos < m_src.size()) m_pos++;
                    }
                } else if (sc == '{') depth++;
                else if (sc == '}') depth--;
            }
        } else if (c == '[') {
            advance();
            int depth = 1;
            while (m_pos < m_src.size() && depth > 0) {
                char sc = m_src[m_pos++];
                if (sc == '"') {
                    while (m_pos < m_src.size()) {
                        char st = m_src[m_pos++];
                        if (st == '"') break;
                        if (st == '\\' && m_pos < m_src.size()) m_pos++;
                    }
                } else if (sc == '[') depth++;
                else if (sc == ']') depth--;
            }
        } else {
            // number, bool, null — read until delimiter
            while (m_pos < m_src.size()) {
                c = m_src[m_pos];
                if (c == ',' || c == '}' || c == ']' || c == ' ' ||
                    c == '\t' || c == '\n' || c == '\r') break;
                m_pos++;
            }
        }
    }

public:
    const std::string& m_src;
    size_t m_pos;

    char peek() const {
        return m_pos < m_src.size() ? m_src[m_pos] : '\0';
    }
    void advance() { if (m_pos < m_src.size()) m_pos++; }
    bool expect(char c) {
        if (peek() != c) return false;
        advance();
        return true;
    }
    void skip_ws() {
        while (m_pos < m_src.size() && (m_src[m_pos] == ' ' || m_src[m_pos] == '\t'
                                        || m_src[m_pos] == '\n' || m_src[m_pos] == '\r'))
            m_pos++;
    }

    std::string parse_string() {
        skip_ws();
        if (peek() != '"') return "";
        advance(); // opening quote
        std::string out;
        while (m_pos < m_src.size()) {
            char c = m_src[m_pos++];
            if (c == '"') return out;
            if (c == '\\' && m_pos < m_src.size()) {
                char esc = m_src[m_pos++];
                switch (esc) {
                case '"':  out += '"'; break;
                case '\\': out += '\\'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                default:   out += esc; break;
                }
            } else {
                out += c;
            }
        }
        return out;
    }

    std::string parse_value() {
        skip_ws();
        char c = peek();
        if (c == '"') return parse_string();
        // For bool / number / null — just read until comma, }, ], or whitespace
        std::string val;
        while (m_pos < m_src.size()) {
            c = m_src[m_pos];
            if (c == ',' || c == '}' || c == ']' || c == ' ' || c == '\t'
                || c == '\n' || c == '\r') break;
            val += c;
            m_pos++;
        }
        return val;
    }
};

} // anonymous namespace

// ── Lockfile implementation ─────────────────────────────────────────────

bool Lockfile::load(const std::string& path) {
    m_path = path;
    m_entries.clear();

    std::ifstream file(path);
    if (!file.is_open()) return true; // missing lockfile is OK

    std::stringstream buf;
    buf << file.rdbuf();
    std::string content = buf.str();

    // Parse top-level object: { "version": N, "packages": { ... } }
    JsonParser parser(content);
    std::map<std::string, std::string> top;
    if (!parser.parse_object(top)) return false;

    if (auto it = top.find("version"); it != top.end()) {
        try { m_format_version = std::stoi(it->second); }
        catch (...) { m_format_version = 1; }
    }

    // H12: Use the JsonParser to find the "packages" key properly (skipping
    // strings) instead of a fragile raw content.find("\"packages\"") that
    // could match inside a version string, SHA256 hash, or field value.
    JsonParser top_parser(content);
    size_t pkg_val_pos = top_parser.find_value_pos("packages");
    if (pkg_val_pos == std::string::npos) return true; // no packages

    // Extract just the packages object from the value position
    std::string pkg_json = content.substr(pkg_val_pos);

    JsonParser pkg_parser(pkg_json);
    std::map<std::string, std::map<std::string, std::string>> raw_entries;
    if (!pkg_parser.parse_object_nested(raw_entries)) return false;

    for (const auto& [name, fields] : raw_entries) {
        LockfileEntry entry;
        entry.name = name;

        if (auto it = fields.find("version"); it != fields.end())
            entry.version = it->second;
        if (auto it = fields.find("sha256"); it != fields.end())
            entry.sha256 = it->second;
        if (auto it = fields.find("has_native"); it != fields.end())
            entry.has_native = (it->second == "true");

        // source_modules — stored as comma-separated string for simplicity
        // (we'll use JSON array parsing in a second pass if needed)
        if (auto it = fields.find("source_modules"); it != fields.end()) {
            std::string mods_str = it->second;
            // It might be a JSON array like ["a","b"] — try to parse
            if (!mods_str.empty() && mods_str.front() == '[') {
                // Need to parse array from the original JSON
                // For now, find the array in the raw content
                size_t arr_start = pkg_json.find("\"source_modules\"", pkg_json.find(name));
                if (arr_start != std::string::npos) {
                    size_t arr_colon = pkg_json.find(':', arr_start);
                    if (arr_colon != std::string::npos) {
                        size_t arr_brace = pkg_json.find('[', arr_colon);
                        if (arr_brace != std::string::npos) {
                            std::string arr_json = pkg_json.substr(arr_brace);
                            JsonParser arr_parser(arr_json);
                            arr_parser.parse_array(entry.source_modules);
                        }
                    }
                }
            } else {
                // Single value or empty
                if (!mods_str.empty()) entry.source_modules.push_back(mods_str);
            }
        }

        // dependencies — nested object
        if (auto it = fields.find("dependencies"); it != fields.end() && it->second == "{") {
            // Need to find the nested dependencies object in raw JSON
            size_t dep_pos = pkg_json.find("\"dependencies\"", pkg_json.find(name));
            if (dep_pos != std::string::npos) {
                size_t dep_colon = pkg_json.find(':', dep_pos);
                if (dep_colon != std::string::npos) {
                    size_t dep_brace = pkg_json.find('{', dep_colon);
                    if (dep_brace != std::string::npos) {
                        std::string dep_json = pkg_json.substr(dep_brace);
                        JsonParser dep_parser(dep_json);
                        dep_parser.parse_object(entry.dependencies);
                    }
                }
            }
        }

        m_entries[name] = std::move(entry);
    }

    return true;
}

bool Lockfile::save() const {
    if (m_path.empty()) return false;

    // M16: write to a temp sibling, then atomically rename over m_path. A crash
    // mid-write previously left m_path half-written and irrecoverable; the
    // temp+rename pattern (POSIX rename is atomic on the same filesystem) keeps
    // the existing lockfile intact until the new one is fully written.
    std::string tmp_path = m_path + ".tmp";
    std::ofstream file(tmp_path);
    if (!file.is_open()) return false;

    file << "{\n";
    file << "  \"version\": " << m_format_version << ",\n";
    file << "  \"packages\": {\n";

    bool first = true;
    for (const auto& [name, entry] : m_entries) {
        if (!first) file << ",\n";
        first = false;

        file << "    \"" << escape_json(name) << "\": {\n";
        file << "      \"version\": \"" << escape_json(entry.version) << "\",\n";
        file << "      \"sha256\": \"" << escape_json(entry.sha256) << "\",\n";
        file << "      \"has_native\": " << (entry.has_native ? "true" : "false") << ",\n";

        // source_modules
        file << "      \"source_modules\": [";
        for (size_t i = 0; i < entry.source_modules.size(); i++) {
            if (i > 0) file << ", ";
            file << "\"" << escape_json(entry.source_modules[i]) << "\"";
        }
        file << "],\n";

        // dependencies
        file << "      \"dependencies\": {";
        bool first_dep = true;
        for (const auto& [dep_name, dep_constraint] : entry.dependencies) {
            if (!first_dep) file << ",";
            first_dep = false;
            file << "\n        \"" << escape_json(dep_name) << "\": \""
                 << escape_json(dep_constraint) << "\"";
        }
        if (!entry.dependencies.empty()) file << "\n      ";
        file << "}\n";

        file << "    }";
    }

    if (!m_entries.empty()) file << "\n";
    file << "  }\n";
    file << "}\n";

    // Flush + close before renaming so all bytes are on disk.
    bool write_ok = file.good();
    file.close();

    if (!write_ok) {
        // Best-effort cleanup of the partial temp file; leave m_path untouched.
        std::error_code ec;
        std::filesystem::remove(tmp_path, ec);
        return false;
    }

    // Atomic on POSIX for same-filesystem renames; overwrites m_path.
    std::error_code ren_ec;
    std::filesystem::rename(tmp_path, m_path, ren_ec);
    if (ren_ec) {
        std::error_code cleanup_ec;
        std::filesystem::remove(tmp_path, cleanup_ec);  // best-effort cleanup
        return false;
    }
    return true;
}

const LockfileEntry* Lockfile::get(const std::string& name) const {
    auto it = m_entries.find(name);
    return (it != m_entries.end()) ? &it->second : nullptr;
}

void Lockfile::set(const std::string& name, const LockfileEntry& entry) {
    m_entries[name] = entry;
}

bool Lockfile::has(const std::string& name) const {
    return m_entries.count(name) > 0;
}

void Lockfile::clear() {
    m_entries.clear();
}

} // namespace angara
