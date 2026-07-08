#include "../../includes/RegistryClient.h"
#include "json.hpp"  // M17: vendored nlohmann/json for strict JSON validation
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

// M18: cap registry metadata responses so a malicious/compromised registry
// can't exhaust memory. Package metadata is tiny JSON; 10 MB is far above any
// realistic payload. Returning 0 aborts the transfer (CURLE_WRITE_ERROR).
static constexpr size_t REGISTRY_MAX_METADATA_BYTES = 10 * 1024 * 1024;

static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    auto* str = static_cast<std::string*>(userp);
    size_t total = size * nmemb;
    if (str->size() + total > REGISTRY_MAX_METADATA_BYTES) return 0;  // M18
    str->append(static_cast<char*>(contents), total);
    return total;
}

// C6: write downloaded bytes into an std::ofstream. Previously http_download
// passed WRITEFUNCTION=nullptr with WRITEDATA=&ofstream — libcurl then used
// its default callback, which calls fwrite() treating WRITEDATA as a FILE*,
// but an ofstream* is not a FILE*. Returning a short count on write failure
// makes libcurl abort the transfer (CURLE_WRITE_ERROR).
static size_t file_write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    auto* out = static_cast<std::ofstream*>(userp);
    size_t total = size * nmemb;
    out->write(static_cast<char*>(contents), static_cast<std::streamsize>(total));
    return out->good() ? total : 0;
}

std::optional<std::string> RegistryClient::http_get(const std::string& url, long* response_code) {
    CURL* curl = curl_easy_init();
    if (!curl) return std::nullopt;

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);                      // M16
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https,http");  // M16
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "angc-pkg/1.0");
    // H15: force TLS peer + host verification regardless of libcurl's default
    // build config, so a MITM can't tamper with registry metadata.
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

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
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, file_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);                      // M16
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https,http");  // M16
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "angc-pkg/1.0");
    // M18: bound a tarball download so a malicious registry can't fill the
    // disk. 100 MB is generous for an Angara package tarball.
    curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE,
                     static_cast<curl_off_t>(100 * 1024 * 1024));
    // H15: force TLS peer + host verification (see http_get above).
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    file.close();

    return res == CURLE_OK;
}

// ── Registry JSON parsing ───────────────────────────────────────────────
//
// M17: replaced a hand-rolled, lax scanner with nlohmann/json, which enforces
// strict JSON syntax and rejects invalid UTF-8 by throwing. A corrupted or
// truncated registry response now yields std::nullopt instead of being
// mis-parsed into empty/garbled fields.
//
// Expected response shape:
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

std::optional<RegistryPackage> RegistryClient::parse_package_json(const std::string& json_text,
                                                                   const std::string& name) {
    using nlohmann::json;

    json root;
    try {
        root = json::parse(json_text);  // throws on malformed JSON / bad UTF-8
    } catch (const json::exception&) {
        return std::nullopt;
    }
    if (!root.is_object()) return std::nullopt;

    RegistryPackage pkg;
    pkg.name = name;

    // "versions" is required and must be an array.
    if (!root.contains("versions") || !root["versions"].is_array()) return std::nullopt;

    for (const auto& v : root["versions"]) {
        if (!v.is_object()) continue;

        // "version" is mandatory and must be a parseable semver string.
        if (!v.contains("version") || !v["version"].is_string()) continue;
        auto parsed_ver = Version::parse(v["version"].get<std::string>());
        if (!parsed_ver) continue;  // skip unparseable version, as before

        RegistryVersion rv;
        rv.version = *parsed_ver;
        rv.sha256 = v.value("sha256", std::string{});
        rv.has_native = v.value("has_native", false);

        // source_modules: array of strings.
        if (v.contains("source_modules") && v["source_modules"].is_array()) {
            for (const auto& m : v["source_modules"]) {
                if (m.is_string()) rv.source_modules.push_back(m.get<std::string>());
            }
        }

        // dependencies: object mapping name -> constraint (string).
        if (v.contains("dependencies") && v["dependencies"].is_object()) {
            for (auto it = v["dependencies"].begin(); it != v["dependencies"].end(); ++it) {
                // Coerce non-string values to their string form so a constraint
                // stored as e.g. a number still round-trips.
                rv.dependencies[it.key()] = it.value().is_string()
                    ? it.value().get<std::string>()
                    : it.value().dump();
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
