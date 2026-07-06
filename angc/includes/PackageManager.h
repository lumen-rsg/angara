#pragma once

#include "Version.h"
#include "Lockfile.h"
#include "RegistryClient.h"
#include "ConfigParser.h"
#include <string>
#include <vector>

namespace angara {

/// A fully resolved and installed package.
struct ResolvedPackage {
    std::string name;
    Version version;
    std::string install_path;    // $ANGARA_HOME/packages/<name>/<version>/
    std::string lib_path;        // .../lib/   (.so/.dylib directory)
    std::string src_path;        // .../src/   (.an modules directory)
    bool has_native = false;
    std::vector<std::string> source_modules; // .an module names provided
};

/// Core package manager: resolves version constraints, downloads packages,
/// maintains the lockfile, and provides package paths to the build system.
class PackageManager {
public:
    /// @param packages_dir  Root directory for installed packages
    ///                      (typically $ANGARA_HOME/packages).
    /// @param registry_url  Optional custom registry URL.
    explicit PackageManager(std::string packages_dir,
                            std::string registry_url = "https://registry.angara-lang.org");

    /// Full workflow: read lockfile (if present), resolve constraints against
    /// the registry, download missing packages, and return resolved paths.
    ///
    /// @param deps         The dependency specifications from the .abs file.
    /// @param project_dir  Directory containing angara.lock and .abs file.
    /// @return             Resolved packages, or empty vector on failure.
    std::vector<ResolvedPackage> resolve_and_install(
        const std::vector<DependencySpec>& deps,
        const std::string& project_dir);

    /// CLI: add a dependency to the .abs file and update the lockfile.
    /// @param abs_path    Path to the .abs file.
    /// @param name        Package name.
    /// @param constraint  Version constraint (e.g. "^1.0.0"). Empty = "*".
    /// @return True on success.
    bool add_dependency(const std::string& abs_path,
                        const std::string& name,
                        const std::string& constraint);

    /// CLI: remove a dependency from the .abs file and update the lockfile.
    /// @param abs_path  Path to the .abs file.
    /// @param name      Package name to remove.
    /// @return True on success.
    bool remove_dependency(const std::string& abs_path, const std::string& name);

    /// CLI: re-resolve all dependencies and update the lockfile.
    /// @param abs_path  Path to the .abs file.
    /// @return True on success.
    bool update_lockfile(const std::string& abs_path);

private:
    std::string m_packages_dir;
    RegistryClient m_registry;

    /// Given a dependency spec, find the best-matching version from the
    /// registry or the lockfile. Resolves transitively.
    bool resolve_one(const DependencySpec& spec,
                     Lockfile& lockfile,
                     std::map<std::string, RegistryVersion>& resolved,
                     std::map<std::string, RegistryPackage>& metadata_cache);

    /// Download and extract a single package.
    bool install_package(const std::string& name,
                         const RegistryVersion& version_info,
                         const std::string& packages_dir);

    /// Check if a package is already installed at the given path.
    bool is_installed(const std::string& name, const std::string& version) const;

    /// Read the ProjectConfig from an .abs file.
    static std::optional<ProjectConfig> read_project_config(const std::string& abs_path);

    /// Write back the ProjectConfig to an .abs file (simple string replacement).
    static bool write_project_dependencies(const std::string& abs_path,
                                            const std::vector<DependencySpec>& deps);
};

} // namespace angara
