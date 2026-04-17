#pragma once

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

    // --- Native C/C++ Module Declaration ---
    struct NativeModuleConfig {
        std::string name;                       // Module name (e.g., "my_glue")
        std::vector<std::string> sources;        // Source files (e.g., ["glue.c", "helper.cpp"])
        std::vector<std::string> include_dirs;   // Additional include paths
        std::vector<std::string> link_libs;      // Libraries to link (e.g., ["curl", "ssl"])
        std::vector<std::string> link_frameworks;// macOS frameworks (e.g., ["OpenGL", "Cocoa"])
        std::string cflags;                      // Extra C compiler flags
        std::string ldflags;                     // Extra linker flags
        bool is_cpp = false;                     // Compile as C++ if true
    };

    // --- Pre/Post Build Step ---
    struct BuildStep {
        std::string command;    // Shell command to execute
        std::string description;// Human-readable description (optional)
    };

    // --- Build Profile ---
    struct BuildProfile {
        BuildMode mode = BuildMode::DEBUG;
        std::string cflags;        // Additional compiler flags
        std::string ldflags;       // Additional linker flags
        std::string output_dir;    // Override output directory (default: project dir)
        std::string target;        // Target triple override
        int opt_level = 0;         // Optimization level (0-3)
    };

    // --- Enhanced Project Config ---
    struct ProjectConfig {
        std::string name;
        std::string path;
        std::string author;
        std::string version;
        std::string description;   // Brief project description
        ProjectType type;
        std::string entry_point;   // e.g., "main.an"
        std::vector<std::string> dependencies;   // Native modules: "io", "json"
        std::vector<NativeModuleConfig> native_modules; // Inline native module builds
        BuildStep pre_build;       // Run before compilation
        BuildStep post_build;      // Run after successful build
        BuildProfile profile;      // Build profile settings
        bool freestanding = false; // -ffreestanding: no libc dependency in runtime
        bool nostdlib = false;     // -nostdlib: don't link standard libraries
    };

    // --- Workspace Configuration ---
    struct WorkspaceConfig {
        std::string name;
        std::string author;
        std::string version;
        std::string description;   // Workspace description
        std::string angara_version;// Minimum required Angara version (optional)
        std::vector<ProjectConfig> projects;
    };

    class ConfigParser {
    public:
        static std::optional<WorkspaceConfig> parse(const std::string& path);
    };

}
