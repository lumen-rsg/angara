#include "../../includes/BuildSystem.h"
#include "CompilerDriver.h"
#include "Colors.h"
#include <iostream>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace angara {

    BuildSystem::BuildSystem()
        : m_native_lib_path(m_angara_home + "/modules"),
          m_std_lib_path(m_angara_home + "/src/modules")
    {}

    bool BuildSystem::build(const std::string& spec_file) {
        auto workspace_opt = ConfigParser::parse(spec_file);
        if (!workspace_opt) return false;

        fs::path spec_path = fs::absolute(spec_file);
        m_workspace_root = spec_path.parent_path().string();

        WorkspaceConfig ws = *workspace_opt;
        std::cout << CLR_BOLD << CLR_MAGENTA << "Building Workspace: " << ws.name << " v" << ws.version << CLR_RESET << "\n";

        // 1. Pre-scan: Map all project names to their absolute ENTRY FILE paths.
        std::map<std::string, std::string> project_entries;
        for (const auto& proj : ws.projects) {
            fs::path project_dir = (fs::path(m_workspace_root) / proj.path).lexically_normal();
            fs::path entry_path = (project_dir / proj.entry_point).lexically_normal();

            project_entries[proj.name] = fs::absolute(entry_path).string();
            m_project_dirs[proj.name] = fs::absolute(project_dir).string();
        }

        // 2. Build each project defined in the .abs file.
        for (const auto& proj : ws.projects) {
            std::cout << "\n" << CLR_BOLD << CLR_BLUE << "--> Project: " << proj.name
                      << " (" << (proj.type == ProjectType::APP ? "App" : "Library") << ")" << CLR_RESET << "\n";

            if (!build_project(proj, project_entries)) {
                std::cerr << CLR_RED << "!!! Failed to build project: " << proj.name << CLR_RESET << "\n";
                return false;
            }
        }

        std::cout << "\n" << CLR_BOLD << CLR_GREEN << "✓ Workspace built successfully." << CLR_RESET << "\n";
        return true;
    }

    bool BuildSystem::build_project(const ProjectConfig& config, const std::map<std::string, std::string>& project_entries) {
        CompilerDriver driver;

        driver.set_paths(m_std_lib_path, m_native_lib_path);
        driver.set_workspace_projects(project_entries);
        if (!m_target_triple.empty()) driver.set_target(m_target_triple);
        if (!m_sysroot.empty()) driver.set_sysroot(m_sysroot);
        if (config.freestanding) driver.set_freestanding(true);
        if (config.nostdlib) driver.set_nostdlib(true);

        const std::string& entry_file = project_entries.at(config.name);

        std::cout << "    Compiling (LLVM)...\n";
        if (!driver.compile(config, entry_file)) {
            return false;
        }

        std::cout << "    Linking artifacts...\n";
        return link_artifacts(config,
                             driver.get_generated_object_files(),
                             driver.get_native_libs_linked(),
                             m_project_dirs[config.name]);
    }

    bool BuildSystem::link_artifacts(const ProjectConfig& config,
                                 const std::set<std::string>& object_files,
                                 const std::vector<std::string>& discovered_libs,
                                 const std::string& project_root) const
    {
        std::stringstream cmd;
        fs::path bin_path = fs::path(project_root) / config.name;

        cmd << "clang";
        if (!m_target_triple.empty()) cmd << " -target " << m_target_triple;
        if (!m_sysroot.empty()) cmd << " --sysroot " << m_sysroot;
        cmd << " -o " << bin_path.string();

        if (config.type == ProjectType::LIBRARY) {
            cmd << " -shared -fPIC";
        }

        // LLVM backend: runtime is embedded in the generated IR — no external runtime needed.
        for (const auto& file : object_files) {
            cmd << " " << file;
        }

        cmd << " -I" << project_root;
        cmd << " -L" << m_native_lib_path;

        // --- SMART LINKING ---
        std::set<std::string> all_libs;
        for (const auto& lib : config.dependencies) all_libs.insert(lib);
        for (const auto& lib : discovered_libs) all_libs.insert(lib);

        for (const auto& lib : all_libs) {
            cmd << " -l" << lib;
        }
        // ----------------------

        if (config.freestanding) {
            // Freestanding: skip host linker, emit object file for bare-metal toolchain
            std::string obj_output = bin_path.string() + ".o";
            if (object_files.size() == 1) {
                fs::rename(*object_files.begin(), obj_output);
            } else {
                obj_output = *object_files.begin();
            }
            std::cout << "    " << CLR_BOLD << CLR_GREEN << "Freestanding object emitted: "
                      << obj_output << CLR_RESET << "\n";
            std::cout << "    " << CLR_CYAN << "Link with your bare-metal toolchain." << CLR_RESET << "\n";
            return true;
        }

        if (config.nostdlib) {
            cmd << " -nostdlib -O2 -Wno-return-type";
        } else {
            cmd << " -pthread -lm -O2 -Wno-return-type";
            cmd << " -Wl,-rpath," << m_native_lib_path;
        }

        if (int result = system(cmd.str().c_str()); result != 0) {
            std::cerr << "    Linker failed for " << config.name << "\n";
            return false;
        }

        // Cleanup object files
        for (const auto& file : object_files) {
            fs::remove(file);
        }

        std::cout << "    ✓ Built " << bin_path.string() << "\n";
        return true;
    }
}