#include "../../includes/Lockfile.h"
#include "json.hpp"  // M17: vendored nlohmann/json for strict JSON validation
#include <fstream>
#include <iostream>
#include <sstream>
#include <filesystem>

namespace angara {

// ── Lockfile implementation ─────────────────────────────────────────────
//
// M17: load() and save() were previously backed by a hand-rolled recursive-
// descent parser / emitter that silently accepted malformed JSON and, in the
// load path, had a latent bug where `dependencies` were effectively never
// read (the guard compared a stripped value to the literal "{"). Both paths
// now use nlohmann/json: strict validation on input (throws on bad JSON / bad
// UTF-8), and a single library for both reading and writing so a round-trip is
// correct by construction. `escape_json` is no longer needed.

bool Lockfile::load(const std::string& path) {
    m_path = path;
    m_entries.clear();

    std::ifstream file(path);
    if (!file.is_open()) return true; // missing lockfile is OK

    std::stringstream buf;
    buf << file.rdbuf();
    std::string content = buf.str();

    using nlohmann::json;
    json root;
    try {
        root = json::parse(content);  // throws on malformed JSON / bad UTF-8
    } catch (const json::exception&) {
        return false;  // corrupted lockfile — refuse to load
    }
    if (!root.is_object()) return false;

    m_format_version = root.value("version", 1);

    // "packages" is optional; an absent or non-object value just yields no entries.
    if (!root.contains("packages") || !root["packages"].is_object()) return true;

    for (auto it = root["packages"].begin(); it != root["packages"].end(); ++it) {
        const json& fields = it.value();
        if (!fields.is_object()) continue;

        LockfileEntry entry;
        entry.name = it.key();
        entry.version = fields.value("version", std::string{});
        entry.sha256 = fields.value("sha256", std::string{});
        entry.has_native = fields.value("has_native", false);

        // source_modules: array of strings.
        if (fields.contains("source_modules") && fields["source_modules"].is_array()) {
            for (const auto& m : fields["source_modules"]) {
                if (m.is_string()) entry.source_modules.push_back(m.get<std::string>());
            }
        }

        // dependencies: object mapping name -> constraint (string).
        if (fields.contains("dependencies") && fields["dependencies"].is_object()) {
            for (auto dep = fields["dependencies"].begin();
                 dep != fields["dependencies"].end(); ++dep) {
                entry.dependencies[dep.key()] = dep.value().is_string()
                    ? dep.value().get<std::string>()
                    : dep.value().dump();
            }
        }

        m_entries[entry.name] = std::move(entry);
    }

    return true;
}

bool Lockfile::save() const {
    if (m_path.empty()) return false;

    // Build the full JSON document in memory first, so a serialization error
    // cannot leave a half-written file on disk.
    using nlohmann::json;
    json packages = json::object();
    for (const auto& [name, entry] : m_entries) {
        json src_modules = json::array();
        for (const auto& m : entry.source_modules) src_modules.push_back(m);

        json deps = json::object();
        for (const auto& [dep_name, dep_constraint] : entry.dependencies) {
            deps[dep_name] = dep_constraint;
        }

        packages[name] = {
            {"version",       entry.version},
            {"sha256",        entry.sha256},
            {"has_native",    entry.has_native},
            {"source_modules", std::move(src_modules)},
            {"dependencies",  std::move(deps)},
        };
    }

    json root = {
        {"version", m_format_version},
        {"packages", std::move(packages)},
    };

    // M16: write to a temp sibling, then atomically rename over m_path. A crash
    // mid-write previously left m_path half-written and irrecoverable; the
    // temp+rename pattern (POSIX rename is atomic on the same filesystem) keeps
    // the existing lockfile intact until the new one is fully written.
    std::string tmp_path = m_path + ".tmp";
    std::ofstream file(tmp_path);
    if (!file.is_open()) return false;

    file << root.dump(2) << "\n";

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
