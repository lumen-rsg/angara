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

private:
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

    // Re-parse to get nested packages object
    // We need a second pass to parse the nested structure.
    // Strategy: find the "packages" key, then parse its object value.
    size_t pkg_pos = content.find("\"packages\"");
    if (pkg_pos == std::string::npos) return true; // no packages

    // Find the colon after "packages"
    size_t colon = content.find(':', pkg_pos);
    if (colon == std::string::npos) return false;

    // Find the opening brace
    size_t brace = content.find('{', colon);
    if (brace == std::string::npos) return false;

    // Extract just the packages object
    std::string pkg_json = content.substr(brace);

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

    std::ofstream file(m_path);
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

    return file.good();
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
