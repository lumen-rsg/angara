#pragma once

#include "ConfigParser.h"
#include "CompilerDriver.h"
#include <string>
#include <vector>
#include <set>
#include <map>

namespace angara {

    class BuildSystem {
    public:
        BuildSystem();

        // --- Main entry points ---
        bool build(const std::string& spec_file);
        bool clean(const std::string& spec_file);
        bool run(const std::string& spec_file);

        // --- Cross-compilation ---
        void set_target(const std::string& triple) { m_target_triple = triple; }
        void set_sysroot(const std::string& path) { m_sysroot = path; }

        // --- Build mode ---
        void set_build_mode(BuildMode mode) { m_build_mode = mode; }

    private:
        // --- System Paths ---
        const std::string m_angara_home = "/opt/angara";
        const std::string m_native_lib_path;   // /opt/angara/modules
        const std::string m_std_lib_path;      // /opt/angara/src/modules

        // --- Build state ---
        std::string m_workspace_root;
        std::string m_build_dir;               // workspace_root/.angara/build
        std::map<std::string, std::string> m_project_dirs;
        std::string m_target_triple;
        std::string m_sysroot;
        BuildMode m_build_mode = BuildMode::DEBUG;

        // --- Core build logic ---
        bool build_project(const ProjectConfig& config,
                           const std::map<std::string, std::string>& project_entries);

        // --- Native module compilation ---
        bool build_native_modules(const ProjectConfig& config,
                                  const std::string& project_root);

        bool compile_native_module(const NativeModuleConfig& mod,
                                   const std::string& project_root);

        // --- Build steps ---
        bool execute_build_step(const BuildStep& step, const std::string& label);

        // --- Linking ---
        bool link_artifacts(const ProjectConfig& config,
                            const std::set<std::string>& object_files,
                            const std::vector<std::string>& discovered_libs,
                            const std::string& project_root) const;

        // --- Build profile resolution ---
        std::string resolve_opt_flags(const BuildProfile& profile) const;
        std::string resolve_output_dir(const ProjectConfig& config) const;

        // --- Utilities ---
        std::string get_module_output_path(const std::string& module_name) const;
    };
}