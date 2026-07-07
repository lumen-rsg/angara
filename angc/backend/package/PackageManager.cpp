#include "../../includes/PackageManager.h"
#include "../../includes/CLI.h"
#include "../../includes/StringUtils.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <cstdlib>

namespace fs = std::filesystem;

namespace angara {

// ── Constructor ─────────────────────────────────────────────────────────

PackageManager::PackageManager(std::string packages_dir, std::string registry_url)
    : m_packages_dir(std::move(packages_dir))
    , m_registry(std::move(registry_url))
{
    fs::create_directories(m_packages_dir);
}

// ── is_installed ────────────────────────────────────────────────────────

bool PackageManager::is_installed(const std::string& name, const std::string& version) const {
    fs::path pkg_path = fs::path(m_packages_dir) / name / version;
    std::error_code ec;
    return fs::exists(pkg_path, ec) && fs::is_directory(pkg_path, ec);
}

// ── install_package ─────────────────────────────────────────────────────

bool PackageManager::install_package(const std::string& name,
                                      const RegistryVersion& version_info,
                                      const std::string& packages_dir) {
    std::string ver_str = version_info.version.to_string();
    fs::path pkg_dir = fs::path(packages_dir) / name / ver_str;

    if (fs::exists(pkg_dir)) {
        // Already installed — could verify sha256 here
        return true;
    }

    // C8: Validate package name and version for path traversal / injection.
    if (name.find("..") != std::string::npos ||
        ver_str.find("..") != std::string::npos) {
        std::cerr << "  [ERROR] Package name or version contains '..' — path traversal rejected.\n";
        return false;
    }
    if (!is_safe_flags(name, "package name") ||
        !is_safe_flags(ver_str, "package version")) {
        std::cerr << "  [ERROR] Package name or version contains dangerous characters.\n";
        return false;
    }

    fs::create_directories(pkg_dir);

    // Download tarball to a temp location
    fs::path tarball = pkg_dir / "package.tar.gz";
    std::cout << "  Downloading " << name << " v" << ver_str << "...\n";

    if (!m_registry.download(name, ver_str, tarball.string())) {
        std::cerr << "  [WARN] Failed to download " << name << " v" << ver_str
                  << ". The registry may not yet be available.\n";
        std::cerr << "         This is expected — the package registry is a planned feature.\n";
        // Don't fail the build; the package may already be available locally.
        // Clean up the empty directory.
        fs::remove_all(pkg_dir);
        return false;
    }

    // C8: Extract tarball with security flags. Use shell_escape on the path,
    // --no-same-owner to prevent setuid bit propagation, --no-overwrite-dir to
    // prevent directory overwrites, and strip-components=0 + explicit dir.
    std::string escaped_dir = shell_escape(pkg_dir.string());
    std::string cmd = "cd " + escaped_dir + " && tar xzf package.tar.gz"
                      " --no-same-owner --no-overwrite-dir 2>/dev/null";
    int result = std::system(cmd.c_str());
    if (result != 0) {
        // Try with gzip explicitly
        cmd = "cd " + escaped_dir + " && gunzip -c package.tar.gz | "
              "tar xf - --no-same-owner --no-overwrite-dir 2>/dev/null";
        result = std::system(cmd.c_str());
    }

    // Remove tarball
    fs::remove(tarball);

    if (result != 0) {
        std::cerr << "  [WARN] Failed to extract package " << name << " v" << ver_str << "\n";
        return false;
    }

    // Verify expected directories exist
    if (version_info.has_native) {
        fs::path lib_dir = pkg_dir / "lib";
        if (!fs::exists(lib_dir)) {
            fs::create_directories(lib_dir);
        }
    }

    if (!version_info.source_modules.empty()) {
        fs::path src_dir = pkg_dir / "src";
        if (!fs::exists(src_dir)) {
            fs::create_directories(src_dir);
        }
    }

    return true;
}

// ── resolve_one ─────────────────────────────────────────────────────────

bool PackageManager::resolve_one(const DependencySpec& spec,
                                  Lockfile& lockfile,
                                  std::map<std::string, RegistryVersion>& resolved,
                                  std::map<std::string, RegistryPackage>& metadata_cache) {
    // Already resolved? Skip.
    if (resolved.count(spec.name)) return true;

    // Parse constraint
    auto constraint = VersionConstraint::parse(spec.version_constraint);
    if (!constraint) {
        std::cerr << "[ERROR] Invalid version constraint '" << spec.version_constraint
                  << "' for package '" << spec.name << "'.\n";
        return false;
    }

    RegistryVersion chosen;

    // Check lockfile first
    if (auto* entry = lockfile.get(spec.name)) {
        auto v = Version::parse(entry->version);
        if (v && constraint->is_satisfied_by(*v)) {
            chosen.version = *v;
            chosen.sha256 = entry->sha256;
            chosen.has_native = entry->has_native;
            chosen.source_modules = entry->source_modules;
            chosen.dependencies = entry->dependencies;

            // Check if installed — if not, we need to download
            if (!is_installed(spec.name, entry->version)) {
                if (!install_package(spec.name, chosen, m_packages_dir)) {
                    // Download failed, try to resolve from registry anyway
                    chosen = RegistryVersion{};
                }
            }
        }
    }

    // If not resolved from lockfile, query registry
    if (chosen.version.major == 0 && chosen.version.minor == 0 && chosen.version.patch == 0
        && chosen.sha256.empty()) {
        // Fetch metadata
        if (!metadata_cache.count(spec.name)) {
            auto meta = m_registry.fetch_metadata(spec.name);
            if (!meta) {
                std::cerr << "[ERROR] Package '" << spec.name
                          << "' not found in registry.\n";
                return false;
            }
            metadata_cache[spec.name] = *meta;
        }

        const auto& meta = metadata_cache[spec.name];

        // Find the best matching version: highest version that satisfies constraint
        const RegistryVersion* best = nullptr;
        for (const auto& rv : meta.versions) {
            if (constraint->is_satisfied_by(rv.version)) {
                if (!best || rv.version > best->version) {
                    best = &rv;
                }
            }
        }

        if (!best) {
            std::cerr << "[ERROR] No version of '" << spec.name
                      << "' satisfies constraint '" << spec.version_constraint << "'.\n";
            std::cerr << "         Available versions:";
            for (const auto& rv : meta.versions) {
                std::cerr << " " << rv.version.to_string();
            }
            std::cerr << "\n";
            return false;
        }

        chosen = *best;

        // Download and install
        if (!is_installed(spec.name, best->version.to_string())) {
            if (!install_package(spec.name, chosen, m_packages_dir)) {
                // If download fails, check if there's a locally installed version
                // that satisfies the constraint
                bool found_local = false;
                for (const auto& rv : meta.versions) {
                    if (constraint->is_satisfied_by(rv.version)
                        && is_installed(spec.name, rv.version.to_string())) {
                        chosen = rv;
                        found_local = true;
                        break;
                    }
                }
                if (!found_local) {
                    std::cerr << "[ERROR] Cannot install '" << spec.name
                              << "' — network unavailable and no local version found.\n";
                    return false;
                }
            }
        }

        // Update lockfile entry
        LockfileEntry entry;
        entry.name = spec.name;
        entry.version = chosen.version.to_string();
        entry.sha256 = chosen.sha256;
        entry.has_native = chosen.has_native;
        entry.source_modules = chosen.source_modules;
        entry.dependencies = chosen.dependencies;
        lockfile.set(spec.name, entry);
    }

    // Record as resolved
    resolved[spec.name] = chosen;

    // Recursively resolve transitive dependencies
    for (const auto& [dep_name, dep_constraint] : chosen.dependencies) {
        DependencySpec dep_spec;
        dep_spec.name = dep_name;
        dep_spec.version_constraint = dep_constraint;
        if (!resolve_one(dep_spec, lockfile, resolved, metadata_cache)) {
            return false;
        }
    }

    return true;
}

// ── resolve_and_install ─────────────────────────────────────────────────

std::vector<ResolvedPackage> PackageManager::resolve_and_install(
    const std::vector<DependencySpec>& deps,
    const std::string& project_dir) {
    std::vector<ResolvedPackage> result;

    if (deps.empty()) return result;

    // Load lockfile
    Lockfile lockfile;
    std::string lockfile_path = (fs::path(project_dir) / "angara.lock").string();
    lockfile.load(lockfile_path);

    // Resolve all dependencies (transitively)
    std::map<std::string, RegistryVersion> resolved;
    std::map<std::string, RegistryPackage> metadata_cache;

    for (const auto& dep : deps) {
        if (!resolve_one(dep, lockfile, resolved, metadata_cache)) {
            return {}; // resolution failed
        }
    }

    // Save updated lockfile
    lockfile.set_path(lockfile_path);
    lockfile.save();

    // Build result list
    for (const auto& [name, rv] : resolved) {
        ResolvedPackage rp;
        rp.name = name;
        rp.version = rv.version;
        rp.install_path = (fs::path(m_packages_dir) / name / rv.version.to_string()).string();
        rp.lib_path = (fs::path(rp.install_path) / "lib").string();
        rp.src_path = (fs::path(rp.install_path) / "src").string();
        rp.has_native = rv.has_native;
        rp.source_modules = rv.source_modules;
        result.push_back(std::move(rp));
    }

    return result;
}

// ── read_project_config ─────────────────────────────────────────────────

std::optional<ProjectConfig> PackageManager::read_project_config(const std::string& abs_path) {
    auto ws_opt = ConfigParser::parse(abs_path);
    if (!ws_opt || ws_opt->projects.empty()) return std::nullopt;
    return ws_opt->projects[0];
}

// ── write_project_dependencies ──────────────────────────────────────────

bool PackageManager::write_project_dependencies(const std::string& abs_path,
                                                  const std::vector<DependencySpec>& deps) {
    std::ifstream in(abs_path);
    if (!in.is_open()) return false;

    std::stringstream buf;
    buf << in.rdbuf();
    std::string content = buf.str();
    in.close();

    // Build the new dependencies line
    std::string new_deps;
    for (size_t i = 0; i < deps.size(); i++) {
        if (i > 0) new_deps += ", ";
        new_deps += deps[i].name;
        if (!deps[i].version_constraint.empty() && deps[i].version_constraint != "*") {
            new_deps += "@" + deps[i].version_constraint;
        }
    }
    std::string new_line = "dependencies = [" + new_deps + "]";

    // Replace the existing dependencies line
    // Find "dependencies = "
    size_t pos = content.find("dependencies =");
    if (pos == std::string::npos) {
        // No dependencies line yet — add after entry/type
        pos = content.find("entry =");
        if (pos == std::string::npos) pos = content.find("type =");
        if (pos != std::string::npos) {
            pos = content.find('\n', pos);
            if (pos != std::string::npos) {
                content.insert(pos + 1, new_line + "\n");
            }
        }
    } else {
        // Find end of existing line
        size_t end = content.find('\n', pos);
        if (end == std::string::npos) end = content.size();
        content.replace(pos, end - pos, new_line);
    }

    std::ofstream out(abs_path);
    if (!out.is_open()) return false;
    out << content;
    return true;
}

// ── add_dependency ──────────────────────────────────────────────────────

bool PackageManager::add_dependency(const std::string& abs_path,
                                     const std::string& name,
                                     const std::string& constraint) {
    auto proj_opt = read_project_config(abs_path);
    if (!proj_opt) {
        std::cerr << "[ERROR] Could not read project file '" << abs_path << "'.\n";
        return false;
    }

    auto deps = proj_opt->dependencies;

    // Check if already present
    for (auto& dep : deps) {
        if (dep.name == name) {
            // Update constraint if different
            if (!constraint.empty() && constraint != "*") {
                dep.version_constraint = constraint;
                return write_project_dependencies(abs_path, deps);
            }
            std::cout << "Package '" << name << "' is already a dependency.\n";
            return true;
        }
    }

    DependencySpec new_dep;
    new_dep.name = name;
    new_dep.version_constraint = constraint.empty() ? "*" : constraint;
    deps.push_back(new_dep);

    if (!write_project_dependencies(abs_path, deps)) {
        std::cerr << "[ERROR] Could not update project file.\n";
        return false;
    }

    // Resolve and update lockfile
    fs::path project_dir = fs::path(abs_path).parent_path();
    auto resolved = resolve_and_install(deps, project_dir.string());
    if (resolved.empty() && !deps.empty()) {
        std::cerr << "[WARN] Could not resolve all dependencies — lockfile not updated.\n";
        // Don't fail; the add was successful even if resolution failed
    }

    std::cout << "Added dependency '" << name;
    if (!constraint.empty() && constraint != "*") {
        std::cout << "@" << constraint;
    }
    std::cout << "'.\n";
    return true;
}

// ── remove_dependency ───────────────────────────────────────────────────

bool PackageManager::remove_dependency(const std::string& abs_path, const std::string& name) {
    auto proj_opt = read_project_config(abs_path);
    if (!proj_opt) {
        std::cerr << "[ERROR] Could not read project file '" << abs_path << "'.\n";
        return false;
    }

    auto deps = proj_opt->dependencies;
    auto it = std::find_if(deps.begin(), deps.end(), [&](const DependencySpec& d) {
        return d.name == name;
    });

    if (it == deps.end()) {
        std::cerr << "[WARN] Package '" << name << "' is not in the dependency list.\n";
        return true;
    }

    deps.erase(it);

    if (!write_project_dependencies(abs_path, deps)) {
        std::cerr << "[ERROR] Could not update project file.\n";
        return false;
    }

    // Update lockfile
    fs::path project_dir = fs::path(abs_path).parent_path();
    auto resolved = resolve_and_install(deps, project_dir.string());

    std::cout << "Removed dependency '" << name << "'.\n";
    return true;
}

// ── update_lockfile ─────────────────────────────────────────────────────

bool PackageManager::update_lockfile(const std::string& abs_path) {
    auto proj_opt = read_project_config(abs_path);
    if (!proj_opt) {
        std::cerr << "[ERROR] Could not read project file '" << abs_path << "'.\n";
        return false;
    }

    // Remove existing lockfile to force re-resolution
    fs::path project_dir = fs::path(abs_path).parent_path();
    std::string lockfile_path = (project_dir / "angara.lock").string();
    if (fs::exists(lockfile_path)) {
        fs::remove(lockfile_path);
    }

    auto resolved = resolve_and_install(proj_opt->dependencies, project_dir.string());
    if (resolved.empty() && !proj_opt->dependencies.empty()) {
        std::cerr << "[ERROR] Could not resolve dependencies.\n";
        return false;
    }

    std::cout << "Lockfile updated: " << resolved.size() << " package(s) resolved.\n";
    for (const auto& pkg : resolved) {
        std::cout << "  " << pkg.name << " v" << pkg.version.to_string() << "\n";
    }
    return true;
}

} // namespace angara
