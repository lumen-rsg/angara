#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <optional>

namespace angara {

    enum class ProjectType {
        APP,
        LIBRARY
    };

    enum class BuildMode {
        DEBUG,
        RELEASE
    };

    /// Describes an inline native C/C++ module to compile alongside the Angara project.
    struct NativeModuleConfig {
        std::string name;
        std::vector<std::string> sources;
        std::vector<std::string> include_dirs;
        std::vector<std::string> link_libs;
        std::vector<std::string> link_frameworks;
        std::string cflags;
        std::string ldflags;
        bool is_cpp = false;
    };

    /// A shell command to run before or after the build.
    struct BuildStep {
        std::string command;
        std::string description;
    };

    /// Compiler and linker settings for a single project.
    struct BuildProfile {
        BuildMode mode = BuildMode::DEBUG;
        std::string cflags;
        std::string ldflags;
        std::string output_dir;
        std::string target;
        int opt_level = 0;
    };

    /// A versioned dependency specification (from the .abs dependencies list).
    struct DependencySpec {
        std::string name;
        std::string version_constraint = "*";  // "*" means any version
    };

    /// Full configuration for a single project within a workspace.
    struct ProjectConfig {
        std::string name;
        std::string path;
        std::string author;
        std::string version;
        std::string description;
        ProjectType type;
        std::string entry_point;
        std::vector<DependencySpec> dependencies;
        std::vector<NativeModuleConfig> native_modules;
        BuildStep pre_build;
        BuildStep post_build;
        BuildProfile profile;
        bool freestanding = false;
        bool freestanding_alloc = false;        // F11: built-in bump allocator
        uint64_t freestanding_alloc_size = 0;   // F11: heap bytes (0 = default 1 MiB)
        bool nostdlib = false;
    };

    /// Top-level workspace configuration parsed from a .abs file.
    struct WorkspaceConfig {
        std::string name;
        std::string author;
        std::string version;
        std::string description;
        std::string angara_version;
        std::vector<ProjectConfig> projects;
    };

    /// Parses .abs (Angara Build Specification) files into a WorkspaceConfig.
    class ConfigParser {
    public:
        /// Parses the given .abs file and returns the workspace configuration.
        /// @param path  Path to the .abs file.
        /// @return The parsed workspace, or std::nullopt if the file could not be opened.
        static std::optional<WorkspaceConfig> parse(const std::string& path);
    };

}
