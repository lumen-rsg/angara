#pragma once

#include "ConfigParser.h"
#include "CompilerDriver.h"
#include <string>
#include <vector>
#include <set>

namespace angara {

    class BuildSystem {
    public:
        BuildSystem();

        // The main entry point
        bool build(const std::string& spec_file);

        // Cross-compilation
        void set_target(const std::string& triple) { m_target_triple = triple; }
        void set_sysroot(const std::string& path) { m_sysroot = path; }

    private:
        // System Paths
        const std::string m_angara_home = "/opt/angara";
        const std::string m_native_lib_path;   // /opt/angara/modules
        const std::string m_std_lib_path;      // /opt/angara/src/modules

        // Build logic
        bool build_project(const ProjectConfig& config,
                           const std::map<std::string, std::string>& project_entries);

        // Invokes clang to link .o files into final output
        bool link_artifacts(const ProjectConfig& config,
                        const std::set<std::string>& object_files,
                        const std::vector<std::string>& discovered_libs,
                        const std::string& project_root) const;

        std::string m_workspace_root;
        std::map<std::string, std::string> m_project_dirs;
        std::string m_target_triple;
        std::string m_sysroot;
    };
}