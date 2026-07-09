#include "../../includes/BuildSystem.h"
#include "CLI.h"
#include "CompilerDriver.h"
#include "Colors.h"
#include "StringUtils.h"
#include "Platform.h"
#include <iostream>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <cstdlib>
#include <chrono>
#include <iomanip>

namespace fs = std::filesystem;

namespace angara {

    BuildSystem::BuildSystem()
        : m_native_lib_path(angara_home() + "/modules"),
          m_std_lib_path(angara_home() + "/src/modules"),
          m_packages_dir(angara_home() + "/packages"),
          m_pkg_manager(m_packages_dir)
    {}

    static constexpr const char* SO_EXT = ANGARA_SO_EXT;

    std::string BuildSystem::get_module_output_path(const std::string& module_name) const {
        return (fs::path(m_build_dir) / "modules" / (module_name + SO_EXT)).string();
    }

    std::string BuildSystem::resolve_opt_flags(const BuildProfile& profile) const {
        std::string flags;
        int opt = profile.opt_level;
        if (opt == 0 && profile.mode == BuildMode::RELEASE) opt = 2;
        switch (opt) {
            case 1: flags = "-O1"; break;
            case 2: flags = "-O2"; break;
            case 3: flags = "-O3"; break;
            default: flags = "-O0"; break;
        }
        return flags;
    }

    std::string BuildSystem::resolve_output_dir(const ProjectConfig& config) const {
        if (!config.profile.output_dir.empty()) {
            fs::path out = fs::path(m_workspace_root) / config.profile.output_dir;
            return out.string();
        }
        return m_project_dirs.count(config.name) ? m_project_dirs.at(config.name) : m_workspace_root;
    }

    bool BuildSystem::execute_build_step(const BuildStep& step, const std::string& label) {
        if (step.command.empty()) return true;

        // C5: pre/post-build commands are shell commands from the .abs file.
        // Refuse to run them unless the user opted in with --allow-build-steps,
        // so a cloned/modified .abs can't execute arbitrary code on build.
        if (!m_allow_build_steps) {
            std::cerr << CLR_RED << "[SECURITY] Refusing to run " << label << " step from '"
                      << (m_workspace_root.empty() ? "<workspace>" : m_workspace_root)
                      << "' — pre/post-build commands are disabled by default.\n"
                      << "            Command: " << step.command << "\n"
                      << CLR_YELLOW << "            To allow it, rebuild with --allow-build-steps "
                      << "(only if you trust this project file)." << CLR_RESET << "\n";
            return false;
        }

        std::string desc = step.description.empty() ? step.command : step.description;
        std::cout << CLR_BOLD << CLR_YELLOW << "[ST] " << CLR_RESET << desc << "\n";

        // C5: run via popen instead of std::system. Both invoke the shell
        // (/bin/sh -c on Unix, cmd /c on Windows), so &&/pipes/redirects keep
        // working — but popen is the codebase's existing convention
        // (TestRunner.cpp), lets us drain the child's stdout, and avoids the
        // raw std::system() call. pclose returns the implementation-defined
        // status (0 on success).
        FILE* pipe = popen(step.command.c_str(), "r");
        if (!pipe) {
            std::cerr << CLR_RED << "[ERROR] Build step '" << label
                      << "' could not be started (popen failed).\n" << CLR_RESET;
            return false;
        }
        // Drain child output so a long-running command doesn't block on a full pipe.
        char buf[4096];
        while (fgets(buf, sizeof(buf), pipe)) {
            std::cout << buf;
        }
        std::cout.flush();
        int result = pclose(pipe);
        if (result != 0) {
            std::cerr << CLR_RED << "[ERROR] Build step '" << label << "' exited with code " << result << ".\n"
                      << "         Command: " << step.command << CLR_RESET << "\n";
            return false;
        }
        return true;
    }

    std::string BuildSystem::format_file_size(std::uintmax_t bytes) {
        const char* units[] = {"B", "KB", "MB", "GB"};
        int unit_idx = 0;
        double size = static_cast<double>(bytes);
        while (size >= 1024.0 && unit_idx < 3) {
            size /= 1024.0;
            unit_idx++;
        }
        std::ostringstream oss;
        if (unit_idx == 0) {
            oss << bytes << " " << units[unit_idx];
        } else {
            oss << std::fixed << std::setprecision(1) << size << " " << units[unit_idx];
        }
        return oss.str();
    }

    void BuildSystem::print_build_summary() const {
        if (m_built_artifacts.empty()) return;

        size_t max_name = 0;
        for (const auto& artifact : m_built_artifacts) {
            fs::path p(artifact);
            size_t len = p.filename().string().size();
            if (len > max_name) max_name = len;
        }
        if (max_name < 4) max_name = 4;

        std::string header = " Artifacts ";
        int total_width = static_cast<int>(max_name) + 14;
        std::string dash_fill(std::max(0, total_width - static_cast<int>(header.size())), '-');

        std::cout << "\n " << CLR_BOLD << CLR_CYAN << "+-" << header << dash_fill << "+" << CLR_RESET << "\n";

        for (const auto& artifact : m_built_artifacts) {
            fs::path p(artifact);
            std::string name = p.filename().string();
            std::string size_str;
            try {
                size_str = format_file_size(fs::file_size(p));
            } catch (...) {
                size_str = "?";
            }
            int padding = static_cast<int>(max_name) - static_cast<int>(name.size());
            std::cout << " " << CLR_CYAN << "|" << CLR_RESET
                      << "  " << CLR_BOLD << name << CLR_RESET
                      << std::string(std::max(0, padding), ' ')
                      << "  " << CLR_GRAY << size_str << CLR_RESET
                      << " " << CLR_CYAN << "|" << CLR_RESET << "\n";
        }

        std::string bottom_fill(total_width + 2, '-');
        std::cout << " " << CLR_BOLD << CLR_CYAN << "+" << bottom_fill << "+" << CLR_RESET << "\n";
    }

    void BuildSystem::print_publish_easter_egg() const {
        std::cout << "\n";
        std::cout << CLR_BOLD << CLR_CYAN
                  << "  \u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\n"
                  << CLR_RESET;
        std::cout << CLR_BOLD << CLR_YELLOW
                  << "    \"C# developers need NuGet, MSBuild, and dotnet publish\n"
                  << "     just to say Hello World. We kept it simple.\"\n"
                  << CLR_RESET;
        std::cout << CLR_MAGENTA
                  << "                          \u2014 The Angara Compiler\n"
                  << CLR_RESET;
        std::cout << CLR_BOLD << CLR_CYAN
                  << "  \u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\n"
                  << CLR_RESET;
        std::cout << "\n";
    }

    bool BuildSystem::build(const std::string& spec_file) {
        auto workspace_opt = ConfigParser::parse(spec_file);
        if (!workspace_opt) return false;

        fs::path spec_path = fs::absolute(spec_file);
        m_workspace_root = spec_path.parent_path().string();
        m_build_dir = (fs::path(m_workspace_root) / ".angara" / "build").string();

        fs::create_directories(m_build_dir);
        fs::create_directories(fs::path(m_build_dir) / "modules");

        WorkspaceConfig ws = *workspace_opt;

        std::string version_str = ws.name + " v" + ws.version;
        int inner = static_cast<int>(version_str.size()) > 0 ? static_cast<int>(version_str.size()) : 20;
        if (inner < 20) inner = 20;
        std::string top_fill(inner + 2, '=');

        std::cout << "\n " << CLR_BOLD << CLR_CYAN << "+" << top_fill << "+" << CLR_RESET << "\n";
        std::cout << " " << CLR_BOLD << CLR_CYAN << "|" << CLR_RESET
                  << "  " << CLR_BOLD << CLR_WHITE << "Angara Build" << CLR_RESET
                  << std::string(std::max(0, inner - 13), ' ')
                  << " " << CLR_BOLD << CLR_CYAN << "|" << CLR_RESET << "\n";
        std::cout << " " << CLR_BOLD << CLR_CYAN << "|" << CLR_RESET
                  << "  " << CLR_BOLD << version_str << CLR_RESET
                  << std::string(std::max(0, inner - static_cast<int>(version_str.size())), ' ')
                  << " " << CLR_BOLD << CLR_CYAN << "|" << CLR_RESET << "\n";
        if (!ws.description.empty()) {
            std::cout << " " << CLR_BOLD << CLR_CYAN << "|" << CLR_RESET
                      << "  " << CLR_GRAY << ws.description << CLR_RESET
                      << std::string(std::max(0, inner - static_cast<int>(ws.description.size())), ' ')
                      << " " << CLR_BOLD << CLR_CYAN << "|" << CLR_RESET << "\n";
        }
        std::cout << " " << CLR_BOLD << CLR_CYAN << "+" << top_fill << "+" << CLR_RESET << "\n";

        auto build_start = std::chrono::high_resolution_clock::now();

        m_built_artifacts.clear();

        std::map<std::string, std::string> project_entries;
        for (const auto& proj : ws.projects) {
            fs::path project_dir = (fs::path(m_workspace_root) / proj.path).lexically_normal();
            fs::path entry_path = (project_dir / proj.entry_point).lexically_normal();

            project_entries[proj.name] = fs::absolute(entry_path).string();
            m_project_dirs[proj.name] = fs::absolute(project_dir).string();
        }

        for (const auto& proj : ws.projects) {
            std::cout << "\n " << CLR_BOLD << CLR_BLUE << "-- " << proj.name;
            if (!proj.description.empty()) {
                std::cout << CLR_GRAY << " -- " << proj.description;
            }
            std::cout << " " << CLR_BLUE << std::string(20, '-') << CLR_RESET << "\n";

            std::cout << "   " << CLR_GRAY
                      << "type: " << (proj.type == ProjectType::APP ? "App" : "Library")
                      << "  |  mode: " << (proj.profile.mode == BuildMode::RELEASE ? "Release" : "Debug")
                      << "  |  opt=" << proj.profile.opt_level
                      << CLR_RESET << "\n";

            if (!build_project(proj, project_entries)) {
                std::cerr << CLR_RED << "[ERROR] Build failed for project '" << proj.name << "'." << CLR_RESET << "\n";
                return false;
            }
        }

        auto build_end = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(build_end - build_start).count();

        std::cout << "\n " << CLR_BOLD << CLR_GREEN << "[OK]" << CLR_RESET
                  << " Workspace built in " << std::fixed << std::setprecision(2) << elapsed << "s\n";

        print_build_summary();
        std::cout << "\n";

        return true;
    }

    bool BuildSystem::clean(const std::string& spec_file) {
        auto workspace_opt = ConfigParser::parse(spec_file);
        if (!workspace_opt) return false;

        fs::path spec_path = fs::absolute(spec_file);
        m_workspace_root = spec_path.parent_path().string();
        m_build_dir = (fs::path(m_workspace_root) / ".angara" / "build").string();

        fs::path build_path(m_build_dir);
        if (fs::exists(build_path)) {
            std::cout << CLR_BOLD << CLR_RED << "[CL] " << CLR_RESET << "Removing " << m_build_dir << "\n";
            fs::remove_all(build_path);
            std::cout << CLR_BOLD << CLR_GREEN << "[OK] " << CLR_RESET << "Build directory removed.\n";
        } else {
            std::cout << CLR_YELLOW << "[OK] " << CLR_RESET << "Build directory not found — already clean.\n";
        }
        return true;
    }

    bool BuildSystem::run(const std::string& spec_file) {
        auto workspace_opt = ConfigParser::parse(spec_file);
        if (!workspace_opt) return false;

        if (!build(spec_file)) return false;

        WorkspaceConfig ws = *workspace_opt;
        for (const auto& proj : ws.projects) {
            if (proj.type == ProjectType::APP) {
                fs::path spec_path = fs::absolute(spec_file);
                std::string workspace_root = spec_path.parent_path().string();
                fs::path project_dir = (fs::path(workspace_root) / proj.path).lexically_normal();
                fs::path binary = project_dir / proj.name;

                if (!fs::exists(binary)) {
                    std::cerr << CLR_RED << "[ERROR] Compiled binary not found at '" << binary.string() << "'.\n"
                              << "         The build may have failed silently." << CLR_RESET << "\n";
                    return false;
                }

                std::cout << "\n " << CLR_BOLD << CLR_CYAN << "[RN] " << CLR_RESET << "Running " << proj.name << "...\n\n";
                int result = std::system(angara::shell_escape(binary.string()).c_str());
                return result == 0;
            }
        }

        std::cerr << CLR_RED << "[ERROR] No executable project found in workspace.\n"
                  << "         Add a project with 'type = app' to the .abs file." << CLR_RESET << "\n";
        return false;
    }

    bool BuildSystem::publish(const std::string& spec_file, const std::string& output_dir) {
        auto workspace_opt = ConfigParser::parse(spec_file);
        if (!workspace_opt) return false;

        fs::path spec_path = fs::absolute(spec_file);
        m_workspace_root = spec_path.parent_path().string();

        fs::path publish_dir;
        if (!output_dir.empty()) {
            publish_dir = fs::absolute(output_dir);
        } else {
            publish_dir = fs::path(m_workspace_root) / ".angara" / "publish";
        }

        std::cout << CLR_BOLD << CLR_MAGENTA << "[PB] " << CLR_RESET
                  << "Publishing " << workspace_opt->name << " v" << workspace_opt->version << "\n";

        if (!build(spec_file)) {
            std::cerr << CLR_RED << "[ERROR] Cannot publish \u2014 build failed." << CLR_RESET << "\n";
            return false;
        }

        fs::create_directories(publish_dir);

        int published_count = 0;

        for (const auto& proj : workspace_opt->projects) {
            fs::path project_dir = (fs::path(m_workspace_root) / proj.path).lexically_normal();
            fs::path artifact_path;

            if (proj.type == ProjectType::LIBRARY) {
                std::string lib_name = "lib" + proj.name + SO_EXT;
                fs::path in_project = project_dir / lib_name;
                fs::path in_build = fs::path(m_build_dir) / "modules" / lib_name;

                if (fs::exists(in_project)) {
                    artifact_path = in_project;
                } else if (fs::exists(in_build)) {
                    artifact_path = in_build;
                } else {
                    fs::path alt_name = project_dir / (proj.name + SO_EXT);
                    if (fs::exists(alt_name)) {
                        artifact_path = alt_name;
                    }
                }
            } else {
                artifact_path = project_dir / proj.name;
            }

            if (!artifact_path.empty() && fs::exists(artifact_path)) {
                fs::path dest = publish_dir / artifact_path.filename();
                fs::copy_file(artifact_path, dest, fs::copy_options::overwrite_existing);

                std::cout << CLR_BOLD << CLR_GREEN << "[OK] " << CLR_RESET
                          << CLR_BOLD << proj.name << CLR_RESET
                          << CLR_GRAY << " \u2192 " << CLR_RESET
                          << dest.string() << "\n";
                published_count++;
            } else {
                std::cout << CLR_YELLOW << "[SKIP] " << CLR_RESET
                          << proj.name << " (no artifact found)\n";
            }
        }

        fs::path local_mod_dir = fs::path(m_build_dir) / "modules";
        if (fs::exists(local_mod_dir)) {
            for (const auto& entry : fs::directory_iterator(local_mod_dir)) {
                if (entry.is_regular_file()) {
                    fs::path dest = publish_dir / entry.path().filename();
                    fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing);

                    std::cout << CLR_BOLD << CLR_GREEN << "[OK] " << CLR_RESET
                              << entry.path().filename().string()
                              << CLR_GRAY << " \u2192 " << CLR_RESET
                              << dest.string()
                              << CLR_GRAY << " (native)" << CLR_RESET << "\n";
                    published_count++;
                }
            }
        }

        if (published_count == 0) {
            std::cerr << CLR_YELLOW << "[WARN] No publishable artifacts were found." << CLR_RESET << "\n";
            return false;
        }

        std::cout << "\n " << CLR_BOLD << CLR_GREEN << "[OK]" << CLR_RESET
                  << " Published " << published_count << " target(s) to '"
                  << publish_dir.string() << "'\n";

        print_publish_easter_egg();

        return true;
    }

    bool BuildSystem::build_project(const ProjectConfig& config,
                                    const std::map<std::string, std::string>& project_entries) {
        std::string project_root = m_project_dirs[config.name];

        if (!execute_build_step(config.pre_build, "pre-build")) {
            return false;
        }

        if (!build_native_modules(config, project_root)) {
            return false;
        }

        // TOOL-1: resolve and install package dependencies.
        if (!resolve_dependencies(config, project_root)) {
            return false;
        }

        CompilerDriver driver;
        driver.set_paths(m_std_lib_path, m_native_lib_path);
        driver.set_workspace_projects(project_entries);

        // TOOL-1: add package source directories to compiler search paths.
        for (const auto& pkg : m_resolved_packages) {
            if (!pkg.source_modules.empty()) {
                driver.add_package_search_path(pkg.src_path);
            }
        }

        if (!m_target_triple.empty()) driver.set_target(m_target_triple);
        else if (!config.profile.target.empty()) driver.set_target(config.profile.target);
        if (!m_sysroot.empty()) driver.set_sysroot(m_sysroot);
        if (config.freestanding) driver.set_freestanding(true);
        if (config.nostdlib) driver.set_nostdlib(true);
        if (m_build_mode == BuildMode::DEBUG) driver.set_debug(true);

        // TOOL-2: pass parallel / incremental flags from the build system.
        if (m_jobs > 0) driver.set_jobs(m_jobs);
        if (m_force_rebuild) driver.set_force_rebuild(true);
        driver.set_build_dir(m_build_dir + "/obj");

        const std::string& entry_file = project_entries.at(config.name);

        std::cout << "   " << CLR_GREEN << "[CX] " << CLR_RESET << "Compiling \u2014 " << entry_file << "\n";
        if (!driver.compile(config, entry_file)) {
            return false;
        }

        std::cout << "   " << CLR_CYAN << "[LK] " << CLR_RESET
                  << "Linking \u2014 " << driver.get_generated_object_files().size() << " objects\n";
        if (!link_artifacts(config,
                            driver.get_generated_object_files(),
                            driver.get_native_libs_linked(),
                            project_root)) {
            return false;
        }

        std::string output_dir = resolve_output_dir(config);
        fs::path bin_path = fs::path(output_dir) / config.name;
        if (fs::exists(bin_path)) {
            m_built_artifacts.push_back(bin_path.string());
        }

        if (!execute_build_step(config.post_build, "post-build")) {
            return false;
        }

        return true;
    }

    bool BuildSystem::build_native_modules(const ProjectConfig& config,
                                           const std::string& project_root) {
        if (config.native_modules.empty()) return true;

        std::cout << "   " << CLR_CYAN << "[NM] " << CLR_RESET
                  << config.native_modules.size() << " native module(s)\n";

        for (const auto& mod : config.native_modules) {
            if (!compile_native_module(mod, project_root)) {
                std::cerr << CLR_RED << "[ERROR] Failed to build native module '" << mod.name << "'." << CLR_RESET << "\n";
                return false;
            }
        }
        return true;
    }

    bool BuildSystem::compile_native_module(const NativeModuleConfig& mod,
                                            const std::string& project_root) {
        std::cout << "   " << CLR_CYAN << "[NM] " << CLR_RESET
                  << mod.name
                  << " (" << (mod.is_cpp ? "C++" : "C") << ", "
                  << mod.sources.size() << " sources)\n";

        std::string compiler = mod.is_cpp ? "clang++" : "clang";

        std::vector<fs::path> resolved_sources;
        for (const auto& src : mod.sources) {
            fs::path src_path = fs::path(project_root) / src;
            if (!fs::exists(src_path)) {
                std::cerr << CLR_RED << "[ERROR] Source file not found: '" << src_path.string() << "'.\n"
                          << "         Declared in native module '" << mod.name << "'. Check that the file exists relative to the project root." << CLR_RESET << "\n";
                return false;
            }
            resolved_sources.push_back(src_path);
        }

        std::vector<std::string> object_files;
        fs::path obj_dir = fs::path(m_build_dir) / "obj" / mod.name;
        fs::create_directories(obj_dir);

        for (const auto& src_path : resolved_sources) {
            std::string base = src_path.stem().string();
            fs::path obj_path = obj_dir / (base + ".o");

            std::stringstream cmd;
            cmd << compiler << " -fPIC -Wall";
            if (mod.is_cpp) cmd << " -std=c++23";

            cmd << " -I" << project_root;
            for (const auto& inc : mod.include_dirs) {
                cmd << " -I" << (fs::path(project_root) / inc).string();
            }

            if (!mod.cflags.empty()) {
                if (is_safe_flags(mod.cflags, "cflags"))
                    cmd << " " << mod.cflags;
            }

            cmd << " -c " << angara::shell_escape(src_path.string()) << " -o " << angara::shell_escape(obj_path.string());

            std::cout << "     " << CLR_GREEN << "[CC] " << CLR_RESET << src_path.filename().string() << "\n";

            int result = std::system(cmd.str().c_str());
            if (result != 0) {
                std::cerr << CLR_RED << "[ERROR] Native compilation failed for '" << src_path.filename().string() << "'.\n"
                          << "         Compiler: " << compiler << ". See compiler output above for details." << CLR_RESET << "\n";
                return false;
            }
            object_files.push_back(obj_path.string());
        }

        fs::path mod_output = fs::path(m_build_dir) / "modules" / (mod.name + SO_EXT);

        std::stringstream link_cmd;
        link_cmd << compiler << " -shared";
        for (const auto& obj : object_files) {
            link_cmd << " " << angara::shell_escape(obj);
        }

        for (const auto& lib : mod.link_libs) {
            link_cmd << " -l" << lib;
        }

        for (const auto& fw : mod.link_frameworks) {
            link_cmd << " -framework " << fw;
        }

        if (!mod.ldflags.empty()) {
            if (is_safe_flags(mod.ldflags, "ldflags"))
                link_cmd << " " << mod.ldflags;
        }

#if defined(__APPLE__)
        link_cmd << " -Wl,-install_name,@rpath/lib" << mod.name << SO_EXT;
#elif defined(_WIN32)
        // Windows DLL: export all symbols (MinGW/clang default to not exporting)
        link_cmd << " -Wl,--export-all-symbols";
#endif

        link_cmd << " -o " << angara::shell_escape(mod_output.string());

        std::cout << "     " << CLR_MAGENTA << "[LD] " << CLR_RESET << mod.name << SO_EXT << "\n";

        int result = std::system(link_cmd.str().c_str());
        if (result != 0) {
            std::cerr << CLR_RED << "[ERROR] Linking failed for native module '" << mod.name << "'.\n"
                      << "         Check that all required libraries and frameworks are available." << CLR_RESET << "\n";
            return false;
        }

        std::cout << "   " << CLR_BOLD << CLR_GREEN << "[OK] " << CLR_RESET << mod.name << SO_EXT << "\n";
        return true;
    }

    // ── TOOL-1: resolve_dependencies ────────────────────────────────────────
    // Calls the package manager to resolve version constraints, download
    // packages, and update the lockfile.  Populates m_resolved_packages.

    bool BuildSystem::resolve_dependencies(const ProjectConfig& config,
                                            const std::string& project_dir) {
        if (config.dependencies.empty()) return true;

        std::cout << "   " << CLR_CYAN << "[PK] " << CLR_RESET
                  << "Resolving " << config.dependencies.size() << " package(s)...\n";

        m_resolved_packages = m_pkg_manager.resolve_and_install(
            config.dependencies, project_dir);

        if (m_resolved_packages.empty() && !config.dependencies.empty()) {
            // Resolution failed — but don't fail the build; deps might be
            // satisfied by system-installed modules or workspace projects.
            std::cout << "   " << CLR_YELLOW << "[WARN] " << CLR_RESET
                      << "Package resolution incomplete — falling back to local modules.\n";
            return true;
        }

        for (const auto& pkg : m_resolved_packages) {
            std::cout << "     " << CLR_GREEN << "[OK] " << CLR_RESET
                      << pkg.name << " v" << pkg.version.to_string();
            if (!pkg.source_modules.empty()) {
                std::cout << CLR_GRAY << " (source: "
                          << pkg.source_modules.size() << " module"
                          << (pkg.source_modules.size() > 1 ? "s" : "") << ")"
                          << CLR_RESET;
            }
            if (pkg.has_native) {
                std::cout << CLR_GRAY << " (native)" << CLR_RESET;
            }
            std::cout << "\n";
        }

        return true;
    }

    bool BuildSystem::link_artifacts(const ProjectConfig& config,
                                     const std::set<std::string>& object_files,
                                     const std::vector<std::string>& discovered_libs,
                                     const std::string& project_root) const
    {
        std::string output_dir = resolve_output_dir(config);
        fs::path bin_path = fs::path(output_dir) / config.name;

        std::stringstream cmd;
        cmd << "clang";

        if (!m_target_triple.empty()) cmd << " -target " << angara::shell_escape(m_target_triple);
        else if (!config.profile.target.empty()) cmd << " -target " << angara::shell_escape(config.profile.target);
        if (!m_sysroot.empty()) cmd << " --sysroot " << angara::shell_escape(m_sysroot);

        cmd << " " << resolve_opt_flags(config.profile);

        if (m_build_mode == BuildMode::DEBUG) cmd << " -g";

        cmd << " -o " << angara::shell_escape(bin_path.string());

        if (config.type == ProjectType::LIBRARY) {
            cmd << " -shared -fPIC";
        } else {
            // RT-6: emit a position-independent executable (matches the PIC
            // relocation model set in LLVMBackend's target machine).
            cmd << " -fPIE -pie";
        }

        for (const auto& file : object_files) {
            cmd << " " << angara::shell_escape(file);
        }

        cmd << " -I" << angara::shell_escape(project_root);
        cmd << " -L" << angara::shell_escape(m_native_lib_path);

        // TOOL-1: add package library paths for native packages.
        for (const auto& pkg : m_resolved_packages) {
            if (pkg.has_native) {
                cmd << " -L" << angara::shell_escape(pkg.lib_path);
            }
        }

        fs::path local_mod_dir = fs::path(m_build_dir) / "modules";
        if (fs::exists(local_mod_dir)) {
            cmd << " -L" << angara::shell_escape(local_mod_dir.string());
        }

        std::set<std::string> all_libs;
        for (const auto& dep : config.dependencies) all_libs.insert(dep.name);
        // Native Angara modules are .so files loaded at runtime via dlopen, not
        // static link libraries. They must NOT be passed as `-l<name>` (that
        // yields "cannot find -l<name>", since lib<name>.so isn't on the link
        // path). Instead, link the .so by its absolute path so the build-time
        // references to `Angara_<name>_Init` / `Angara_<name>_<fn>` resolve, and
        // rely on -rpath (added below) for the runtime loader. This mirrors the
        // single-file compile path (CompileCommands.cpp).
        std::set<std::string> linked_native;
        for (const auto& mod_name : discovered_libs) {
            fs::path local_mod  = fs::path(m_build_dir) / "modules" / (mod_name + ANGARA_NATIVE_EXT);
            fs::path native_mod = fs::path(m_native_lib_path) / (mod_name + ANGARA_NATIVE_EXT);
            fs::path chosen;
            if (fs::exists(local_mod))       chosen = fs::absolute(local_mod);
            else if (fs::exists(native_mod)) chosen = native_mod;
            if (!chosen.empty()) {
                cmd << " " << angara::shell_escape(chosen.string());
                linked_native.insert(mod_name);
            }
            // If neither exists, fall through silently: a later -l<name> from the
            // manifest's link_libs (handled below) may still resolve it.
        }

        // TOOL-1: add native package link flags.
        for (const auto& pkg : m_resolved_packages) {
            if (pkg.has_native) {
                all_libs.insert(pkg.name);
            }
        }

        for (const auto& mod : config.native_modules) {
            for (const auto& lib : mod.link_libs) all_libs.insert(lib);
        }

        for (const auto& lib : all_libs) {
            cmd << " -l" << lib;
        }

        for (const auto& mod : config.native_modules) {
            for (const auto& fw : mod.link_frameworks) {
                cmd << " -framework " << fw;
            }
        }

        if (!config.profile.ldflags.empty()) {
            if (is_safe_flags(config.profile.ldflags, "profile ldflags"))
                cmd << " " << config.profile.ldflags;
        }

        if (config.freestanding) {
            std::string obj_output = bin_path.string() + ".o";
            if (object_files.size() == 1) {
                fs::rename(*object_files.begin(), obj_output);
            } else {
                obj_output = *object_files.begin();
            }
            std::cout << "   " << CLR_BOLD << CLR_GREEN << "[OK] " << CLR_RESET << "Freestanding object emitted: "
                      << obj_output << "\n";
            std::cout << "   " << CLR_CYAN << "     Link with your bare-metal toolchain." << CLR_RESET << "\n";
            return true;
        }

        if (config.nostdlib) {
            cmd << " -nostdlib -Wno-return-type";
        } else {
            cmd << " -pthread -lm -Wno-return-type";
            cmd << " -Wl,-rpath," << m_native_lib_path;
            if (fs::exists(local_mod_dir)) {
                cmd << " -Wl,-rpath," << local_mod_dir.string();
            }
        }

        if (int result = std::system(cmd.str().c_str()); result != 0) {
            std::cerr << CLR_RED << "[ERROR] Linker failed for project '" << config.name << "'.\n"
                      << "         Check that all libraries are installed and paths are correct." << CLR_RESET << "\n";
            return false;
        }

        // TOOL-2: keep .o files for incremental compilation.
        // They live in .angara/build/obj/ and are cleaned by `angc clean`.
        // Single-file builds (no build dir) still delete as before.

        std::cout << "   " << CLR_BOLD << CLR_GREEN << "[OK] " << CLR_RESET << bin_path.string() << "\n";
        return true;
    }
}
