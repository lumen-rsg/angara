#include "../../includes/RegistryClient.h"
#include <curl/curl.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <cstring>

namespace angara {

// ── RegistryClient constructor ──────────────────────────────────────────

RegistryClient::RegistryClient(std::string registry_url)
    : m_registry_url(std::move(registry_url))
{
    // Remove trailing slash for clean URL building.
    while (!m_registry_url.empty() && m_registry_url.back() == '/')
        m_registry_url.pop_back();
}

// ── HTTP GET helpers ────────────────────────────────────────────────────

static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    auto* str = static_cast<std::string*>(userp);
    size_t total = size * nmemb;
    str->append(static_cast<char*>(contents), total);
    return total;
}

std::optional<std::string> RegistryClient::http_get(const std::string& url, long* response_code) {
    CURL* curl = curl_easy_init();
    if (!curl) return std::nullopt;

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "angc-pkg/1.0");

    CURLcode res = curl_easy_perform(curl);
    if (response_code) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, response_code);
    }
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) return std::nullopt;
    return response;
}

bool RegistryClient::http_download(const std::string& url, const std::string& dest_path) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    std::ofstream file(dest_path, std::ios::binary);
    if (!file.is_open()) {
        curl_easy_cleanup(curl);
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, nullptr);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "angc-pkg/1.0");

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    file.close();

    return res == CURLE_OK;
}

// ── Registry JSON parsing ───────────────────────────────────────────────

// Minimal JSON parsing for the registry response format:
// {
//   "name": "io",
//   "versions": [
//     {
//       "version": "1.2.3",
//       "sha256": "abc...",
//       "has_native": true,
//       "source_modules": ["io"],
//       "dependencies": {}
//     }
//   ]
// }

namespace {

// Extract the value of a JSON string key. Returns empty if not found.
std::string extract_string(const std::string& json, const std::string& key, size_t start_pos = 0) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search, start_pos);
    if (pos == std::string::npos) return "";

    // Find the colon
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return "";

    // Skip whitespace
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n'))
        pos++;

    if (pos >= json.size() || json[pos] != '"') {
        // Non-string value (number, bool, null) — read until comma/brace/bracket
        size_t end = json.find_first_of(",}]", pos);
        if (end == std::string::npos) end = json.size();
        std::string val = json.substr(pos, end - pos);
        // Trim whitespace
        while (!val.empty() && val.back() == ' ') val.pop_back();
        return val;
    }

    pos++; // skip opening quote
    std::string val;
    while (pos < json.size()) {
        if (json[pos] == '\\') {
            pos++;
            if (pos < json.size()) val += json[pos];
        } else if (json[pos] == '"') {
            break;
        } else {
            val += json[pos];
        }
        pos++;
    }
    return val;
}

// Extract a nested JSON object value for a key. Returns the raw JSON between { and }.
std::string extract_object(const std::string& json, const std::string& key, size_t start_pos = 0) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search, start_pos);
    if (pos == std::string::npos) return "";

    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return "";

    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n'))
        pos++;

    if (pos >= json.size() || json[pos] != '{') return "";

    size_t start = pos;
    int depth = 0;
    while (pos < json.size()) {
        if (json[pos] == '{') depth++;
        else if (json[pos] == '}') {
            depth--;
            if (depth == 0) return json.substr(start, pos - start + 1);
        } else if (json[pos] == '"') {
            pos++;
            while (pos < json.size() && json[pos] != '"') {
                if (json[pos] == '\\') pos++;
                pos++;
            }
        }
        pos++;
    }
    return "";
}

// Extract a JSON array value for a key. Returns comma-separated strings.
std::vector<std::string> extract_array(const std::string& json, const std::string& key, size_t start_pos = 0) {
    std::vector<std::string> result;
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search, start_pos);
    if (pos == std::string::npos) return result;

    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return result;

    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n'))
        pos++;

    if (pos >= json.size() || json[pos] != '[') return result;

    pos++; // skip '['
    while (pos < json.size()) {
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == ','))
            pos++;

        if (pos >= json.size() || json[pos] == ']') break;

        if (json[pos] == '"') {
            pos++; // skip opening quote
            std::string val;
            while (pos < json.size() && json[pos] != '"') {
                if (json[pos] == '\\') pos++;
                if (pos < json.size()) val += json[pos];
                pos++;
            }
            if (pos < json.size()) pos++; // skip closing quote
            if (!val.empty()) result.push_back(val);
        } else {
            pos++; // skip unrecognized char
        }
    }
    return result;
}

} // anonymous namespace

std::optional<RegistryPackage> RegistryClient::parse_package_json(const std::string& json,
                                                                   const std::string& name) {
    RegistryPackage pkg;
    pkg.name = name;

    // Find the "versions" array
    std::string search = "\"versions\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return std::nullopt;

    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return std::nullopt;

    // Find the opening bracket of the versions array
    pos++;
    while (pos < json.size() && json[pos] != '[') pos++;
    if (pos >= json.size()) return std::nullopt;

    // Now iterate through the array finding each version object
    while (pos < json.size()) {
        // Find next '{'
        while (pos < json.size() && json[pos] != '{' && json[pos] != ']') pos++;
        if (pos >= json.size() || json[pos] == ']') break;

        // Parse one version object
        size_t obj_start = pos;
        int depth = 0;
        while (pos < json.size()) {
            if (json[pos] == '{') depth++;
            else if (json[pos] == '}') {
                depth--;
                if (depth == 0) break;
            } else if (json[pos] == '"') {
                pos++;
                while (pos < json.size() && json[pos] != '"') {
                    if (json[pos] == '\\') pos++;
                    pos++;
                }
            }
            pos++;
        }
        if (pos >= json.size()) break;

        std::string obj_json = json.substr(obj_start, pos - obj_start + 1);
        pos++; // move past '}'

        RegistryVersion rv;
        std::string ver_str = extract_string(obj_json, "version");
        if (auto v = Version::parse(ver_str)) {
            rv.version = *v;
        } else {
            continue; // skip unparseable version
        }

        rv.sha256 = extract_string(obj_json, "sha256");
        std::string native_str = extract_string(obj_json, "has_native");
        rv.has_native = (native_str == "true");

        rv.source_modules = extract_array(obj_json, "source_modules");

        // Parse dependencies object
        std::string deps_json = extract_object(obj_json, "dependencies");
        if (!deps_json.empty()) {
            // Parse key-value pairs from deps_json
            size_t dp = 1; // skip opening '{'
            while (dp < deps_json.size()) {
                while (dp < deps_json.size() && deps_json[dp] != '"' && deps_json[dp] != '}')
                    dp++;
                if (dp >= deps_json.size() || deps_json[dp] == '}') break;

                std::string dep_name = extract_string(deps_json, "", dp);
                // Actually we need a better approach — let's parse manually
                dp++; // skip opening quote of key
                std::string key;
                while (dp < deps_json.size() && deps_json[dp] != '"') {
                    if (deps_json[dp] == '\\') dp++;
                    if (dp < deps_json.size()) key += deps_json[dp];
                    dp++;
                }
                dp++; // skip closing quote

                // Find colon
                while (dp < deps_json.size() && deps_json[dp] != ':') dp++;
                dp++; // skip colon

                // Skip whitespace
                while (dp < deps_json.size() && (deps_json[dp] == ' ' || deps_json[dp] == '\t' || deps_json[dp] == '\n'))
                    dp++;

                std::string val;
                if (dp < deps_json.size() && deps_json[dp] == '"') {
                    dp++; // skip opening quote
                    while (dp < deps_json.size() && deps_json[dp] != '"') {
                        if (deps_json[dp] == '\\') dp++;
                        if (dp < deps_json.size()) val += deps_json[dp];
                        dp++;
                    }
                    dp++; // skip closing quote
                }

                if (!key.empty()) {
                    rv.dependencies[key] = val;
                }

                // Skip comma
                while (dp < deps_json.size() && deps_json[dp] != ',' && deps_json[dp] != '}')
                    dp++;
                if (dp < deps_json.size() && deps_json[dp] == ',') dp++;
            }
        }

        pkg.versions.push_back(std::move(rv));
    }

    if (pkg.versions.empty()) return std::nullopt;
    return pkg;
}

// ── Public API ──────────────────────────────────────────────────────────

std::optional<RegistryPackage> RegistryClient::fetch_metadata(const std::string& name) {
    std::string url = m_registry_url + "/v1/packages/" + name;
    auto response = http_get(url);
    if (!response) return std::nullopt;
    return parse_package_json(*response, name);
}

bool RegistryClient::download(const std::string& name, const std::string& version,
                               const std::string& dest_path) {
    std::string url = m_registry_url + "/v1/packages/" + name + "/" + version + "/download";
    return http_download(url, dest_path);
}

} // namespace angara
