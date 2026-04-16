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

    struct ProjectConfig {
        std::string name;
        std::string path;
        std::string author;
        std::string version;
        ProjectType type;
        std::string entry_point; // e.g., "main.an"
        std::vector<std::string> dependencies; // Native modules: "rmq", "json"
        bool freestanding = false;  // -ffreestanding: no libc dependency in runtime
        bool nostdlib = false;      // -nostdlib: don't link standard libraries
    };

    struct WorkspaceConfig {
        std::string name;
        std::string author;
        std::string version;
        std::vector<ProjectConfig> projects;
    };

    class ConfigParser {
    public:
        static std::optional<WorkspaceConfig> parse(const std::string& path);
    };

}