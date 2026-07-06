#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>

namespace angara {

/// A single resolved entry in the lockfile.
struct LockfileEntry {
    std::string name;
    std::string version;
    std::string sha256;
    bool has_native = false;
    std::vector<std::string> source_modules;
    std::map<std::string, std::string> dependencies; // name → version constraint
};

/// JSON lockfile that pins exact dependency versions and checksums.
///
/// Stored as `angara.lock` alongside the project's `.abs` file.
/// Follows the same inline-JSON pattern as BuildManifest for consistency.
class Lockfile {
public:
    /// Load lockfile from disk. Returns true on success (missing file is OK).
    bool load(const std::string& path);

    /// Write lockfile to disk. Returns true on success.
    bool save() const;

    /// Returns the file path this lockfile was loaded from / will save to.
    const std::string& path() const { return m_path; }
    void set_path(const std::string& p) { m_path = p; }

    /// Lockfile format version.
    int format_version() const { return m_format_version; }

    /// All entries.
    const std::map<std::string, LockfileEntry>& entries() const { return m_entries; }

    /// Get an entry by package name, or nullptr.
    const LockfileEntry* get(const std::string& name) const;

    /// Set (insert or update) an entry.
    void set(const std::string& name, const LockfileEntry& entry);

    /// Returns true if the lockfile has an entry for `name`.
    bool has(const std::string& name) const;

    /// Remove all entries (used before re-resolving).
    void clear();

private:
    std::string m_path;
    int m_format_version = 1;
    std::map<std::string, LockfileEntry> m_entries;

    static std::string escape_json(const std::string& s);
};

} // namespace angara
