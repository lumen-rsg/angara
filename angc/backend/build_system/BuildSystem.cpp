#include "../../includes/BuildSystem.h"
#include "CompilerDriver.h" // We still use this for recursive source compilation
#include <iostream>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

const auto RESET   = "\033[0m";
const auto BOLD    = "\033[1m";
const auto RED     = "\033[31m";
const auto GREEN   = "\033[32m";
const auto BLUE    = "\033[34m";
const auto MAGENTA = "\033[35m";

namespace angara {

    BuildSystem::BuildSystem()
        : m_runtime_path(m_angara_home + "/src/runtime"),
          m_native_lib_path(m_angara_home + "/modules"),
          m_std_lib_path(m_angara_home + "/src/modules")
    {}

    // backend/build_system/BuildSystem.cpp

    bool BuildSystem::build(const std::string& spec_file) {
        auto workspace_opt = ConfigParser::parse(spec_file);
        if (!workspace_opt) return false;

        fs::path spec_path = fs::absolute(spec_file);
        m_workspace_root = spec_path.parent_path().string();

        WorkspaceConfig ws = *workspace_opt;
        std::cout << BOLD << MAGENTA << "Building Workspace: " << ws.name << " v" << ws.version << RESET << "\n";

        // 1. Pre-scan: Map all project names to their absolute ENTRY FILE paths.
        // This map is shared with the CompilerDriver so 'attach ProjectName' works.
        std::map<std::string, std::string> project_entries;
        for (const auto& proj : ws.projects) {
            fs::path project_dir = (fs::path(m_workspace_root) / proj.path).lexically_normal();
            fs::path entry_path = (project_dir / proj.entry_point).lexically_normal();

            project_entries[proj.name] = fs::absolute(entry_path).string();
            m_project_dirs[proj.name] = fs::absolute(project_dir).string();
        }

        // 2. Build each project defined in the .abs file.
        for (const auto& proj : ws.projects) {
            std::cout << "\n" << BOLD << BLUE << "--> Project: " << proj.name
                      << " (" << (proj.type == ProjectType::APP ? "App" : "Library") << ")" << RESET << "\n";

            // Pass the specific project configuration to the build worker.
            if (!build_project(proj, project_entries)) {
                std::cerr << RED << "!!! Failed to build project: " << proj.name << RESET << "\n";
                return false;
            }
        }

        std::cout << "\n" << BOLD << GREEN << "✓ Workspace built successfully." << RESET << "\n";
        return true;
    }

    bool BuildSystem::build_project(const ProjectConfig& config, const std::map<std::string, std::string>& project_entries) {
        // 1. Instantiate a fresh driver for this project scope.
        CompilerDriver driver;

        // 2. Configure the driver with system and workspace paths.
        driver.set_paths(m_std_lib_path, m_native_lib_path);
        driver.set_workspace_projects(project_entries);

        // 3. Resolve the entry point for THIS project.
        const std::string& entry_file = project_entries.at(config.name);

        // 4. Trigger the project-aware compilation.
        // This tells the driver: "Even if this file is main.an, the module name is 'Logger'"
        std::cout << "    Transpiling source code...\n";
        if (!driver.compile(config, entry_file)) {
            return false;
        }

        // 5. Link the generated artifacts into the final binary.
        // Use the project directory (m_project_dirs[config.name]) as the destination.
        std::cout << "    Linking artifacts...\n";
        return link_artifacts(config,
                             driver.get_generated_c_files(),
                             driver.get_native_libs_linked(),
                             m_project_dirs[config.name]);
    }

    bool BuildSystem::link_artifacts(const ProjectConfig& config,
                                 const std::set<std::string>& c_files,
                                 const std::vector<std::string>& discovered_libs,
                                 const std::string& project_root) const
    {
        std::stringstream cmd;
        fs::path bin_path = fs::path(project_root) / config.name;

        cmd << "clang -o " << bin_path.string();

        if (config.type == ProjectType::LIBRARY) {
            cmd << " -shared -fPIC";
        }

        for (const auto& file : c_files) {
            cmd << " " << file;
        }

        cmd << " " << m_runtime_path << "/angara_runtime.c";
        cmd << " -I. -I" << project_root << " -I" << m_runtime_path << " -I" << m_std_lib_path;
        cmd << " -L" << m_native_lib_path;

        // --- SMART LINKING ---
        // Use a set to avoid duplicate -l flags
        std::set<std::string> all_libs;
        // 1. Add libs explicitly defined in project.abs
        for (const auto& lib : config.dependencies) all_libs.insert(lib);
        // 2. Add libs automatically discovered by the compiler (e.g., 'io', 'adv_string')
        for (const auto& lib : discovered_libs) all_libs.insert(lib);

        for (const auto& lib : all_libs) {
            cmd << " -l" << lib;
        }
        // ----------------------

        cmd << " -pthread -lm -O2 -Wno-return-type";
        cmd << " -Wl,-rpath," << m_native_lib_path;

        if (int result = system(cmd.str().c_str()); result != 0) {
            std::cerr << "    Linker failed for " << config.name << "\n";
            return false;
        }

        // Cleanup artifacts
        for (const auto& file : c_files) {
            fs::remove(file);
            fs::path h_file = file;
            h_file.replace_extension(".h");
            if (fs::exists(h_file)) fs::remove(h_file);
        }

        std::cout << "    ✓ Built " << bin_path.string() << "\n";
        return true;
    }
}