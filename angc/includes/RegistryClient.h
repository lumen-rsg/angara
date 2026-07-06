#pragma once

#include "Version.h"
#include <string>
#include <vector>
#include <map>
#include <optional>

namespace angara {

/// Version info as returned by the registry for a specific package version.
struct RegistryVersion {
    Version version;
    std::string sha256;
    bool has_native = false;
    std::vector<std::string> source_modules;
    std::map<std::string, std::string> dependencies; // name → constraint
};

/// Metadata about a package on the registry.
struct RegistryPackage {
    std::string name;
    std::vector<RegistryVersion> versions;
};

/// HTTP client for the Angara package registry REST API.
///
/// Registry API:
///   GET /v1/packages/<name>          → package metadata (all versions)
///   GET /v1/packages/<name>/<ver>/download  → tarball download
class RegistryClient {
public:
    /// @param registry_url  Base URL of the registry (default: official registry).
    explicit RegistryClient(std::string registry_url = "https://registry.angara-lang.org");

    /// Fetch package metadata (all available versions + deps).
    /// Returns std::nullopt on network error or 404.
    std::optional<RegistryPackage> fetch_metadata(const std::string& name);

    /// Download a package tarball and save it to `dest_path`.
    /// Returns true on success (HTTP 200, saved to disk).
    bool download(const std::string& name, const std::string& version,
                  const std::string& dest_path);

    /// Returns the current registry URL.
    const std::string& url() const { return m_registry_url; }

private:
    std::string m_registry_url;

    /// Perform an HTTP GET request. Returns the response body on success,
    /// or std::nullopt on failure. `response_code` is set to the HTTP status.
    std::optional<std::string> http_get(const std::string& url, long* response_code = nullptr);

    /// Download a URL to a file. Returns true on success.
    bool http_download(const std::string& url, const std::string& dest_path);

    /// Parse registry JSON response into RegistryPackage.
    static std::optional<RegistryPackage> parse_package_json(const std::string& json,
                                                              const std::string& name);
};

} // namespace angara
