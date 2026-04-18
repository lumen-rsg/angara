#include "../../includes/BuildSystem.h"
#include "CompilerDriver.h"
#include "Colors.h"
#include <iostream>
#include <filesystem>
#include <sstream>
#include <cstdlib>
#include <chrono>
#include <iomanip>

namespace fs = std::filesystem;

namespace angara {

    BuildSystem::BuildSystem()
        : m_native_lib_path(m_angara_home + "/modules"),
          m_std_lib_path(m_angara_home + "/src/modules")
    {}

    // =====================================================
    // Utility Helpers
    // =====================================================

#if defined(__APPLE__)
    static const char* const SO_EXT = ".dylib";
#elif defined(__linux__)
    static const char* const SO_EXT = ".so";
#elif defined(_WIN32)
    static const char* const SO_EXT = ".dll";
#else
    static const char* const SO_EXT = ".so";
#endif

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

    // =====================================================
    // Build Step Execution
    // =====================================================

    bool BuildSystem::execute_build_step(const BuildStep& step, const std::string& label) {
        if (step.command.empty()) return true;

        std::string desc = step.description.empty() ? step.command : step.description;
        std::cout << CLR_BOLD << CLR_YELLOW << "⚙ " << CLR_RESET
                  << CLR_BOLD << "[" << label << "] " << CLR_RESET << desc << "\n";

        int result = std::system(step.command.c_str());
        if (result != 0) {
            std::cerr << CLR_RED << "✗ Build step '" << label << "' failed with code " << result << CLR_RESET << "\n";
            return false;
        }
        return true;
    }

    // =====================================================
    // Utility: Format File Size
    // =====================================================

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

    // =====================================================
    // Build Summary
    // =====================================================

    void BuildSystem::print_build_summary() const {
        if (m_built_artifacts.empty()) return;

        std::cout << "\n" << CLR_BOLD << "📦 Artifacts:" << CLR_RESET << "\n";
        for (const auto& artifact : m_built_artifacts) {
            fs::path p(artifact);
            std::string name = p.filename().string();
            std::string size_str;
            try {
                size_str = format_file_size(fs::file_size(p));
            } catch (...) {
                size_str = "?";
            }
            std::cout << CLR_GREEN << "  • " << CLR_RESET
                      << CLR_BOLD << name << CLR_RESET
                      << CLR_GRAY << "  (" << size_str << ")" << CLR_RESET << "\n";
        }
    }

    // =====================================================
    // Publish Easter Egg (C# inspired)
    // =====================================================

    void BuildSystem::print_publish_easter_egg() const {
        std::cout << "\n";
        std::cout << CLR_BOLD << CLR_CYAN
                  << "  ═════════════════════════════════════════════════════════════════\n"
                  << CLR_RESET;
        std::cout << CLR_BOLD << CLR_YELLOW
                  << "    \"C# developers need NuGet, MSBuild, and dotnet publish\n"
                  << "     just to say Hello World. We kept it simple. 😎\"\n"
                  << CLR_RESET;
        std::cout << CLR_MAGENTA
                  << "                          — The Angara Compiler\n"
                  << CLR_RESET;
        std::cout << CLR_BOLD << CLR_CYAN
                  << "  ═════════════════════════════════════════════════════════════════\n"
                  << CLR_RESET;
        std::cout << "\n";
    }

    // =====================================================
    // Main Build Entry
    // =====================================================

    bool BuildSystem::build(const std::string& spec_file) {
        auto workspace_opt = ConfigParser::parse(spec_file);
        if (!workspace_opt) return false;

        fs::path spec_path = fs::absolute(spec_file);
        m_workspace_root = spec_path.parent_path().string();
        m_build_dir = (fs::path(m_workspace_root) / ".angara" / "build").string();

        // Ensure build directories exist
        fs::create_directories(m_build_dir);
        fs::create_directories(fs::path(m_build_dir) / "modules");

        WorkspaceConfig ws = *workspace_opt;
        std::cout << CLR_BOLD << CLR_MAGENTA << "Building Workspace: " << ws.name << " v" << ws.version << CLR_RESET << "\n";

        if (!ws.description.empty()) {
            std::cout << CLR_GRAY << "  " << ws.description << CLR_RESET << "\n";
        }

        // Start build timer
        auto build_start = std::chrono::high_resolution_clock::now();

        // Clear artifact tracking
        m_built_artifacts.clear();

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
            std::cout << "\n" << CLR_BOLD << CLR_BLUE << "--> Project: " << proj.name;
            if (!proj.description.empty()) {
                std::cout << CLR_GRAY << " — " << proj.description;
            }
            std::cout << CLR_RESET << "\n";
            std::cout << "    (" << (proj.type == ProjectType::APP ? "App" : "Library")
                      << ", " << (proj.profile.mode == BuildMode::RELEASE ? "Release" : "Debug")
                      << ", opt=" << proj.profile.opt_level << ")\n";

            if (!build_project(proj, project_entries)) {
                std::cerr << CLR_RED << "!!! Failed to build project: " << proj.name << CLR_RESET << "\n";
                return false;
            }
        }

        // Build timing
        auto build_end = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(build_end - build_start).count();

        std::cout << "\n" << CLR_BOLD << CLR_GREEN << "✓ Workspace built successfully"
                  << CLR_GRAY << " in " << std::fixed << std::setprecision(1) << elapsed << "s"
                  << CLR_RESET << "\n";

        // Print artifact summary
        print_build_summary();

        return true;
    }

    // =====================================================
    // Clean
    // =====================================================

    bool BuildSystem::clean(const std::string& spec_file) {
        auto workspace_opt = ConfigParser::parse(spec_file);
        if (!workspace_opt) return false;

        fs::path spec_path = fs::absolute(spec_file);
        m_workspace_root = spec_path.parent_path().string();
        m_build_dir = (fs::path(m_workspace_root) / ".angara" / "build").string();

        fs::path build_path(m_build_dir);
        if (fs::exists(build_path)) {
            std::cout << CLR_BOLD << CLR_RED << "[CL] " << CLR_RESET << "Cleaning " << m_build_dir << "\n";
            fs::remove_all(build_path);
            std::cout << CLR_BOLD << CLR_GREEN << "✓ Clean complete." << CLR_RESET << "\n";
        } else {
            std::cout << CLR_YELLOW << "Build directory not found. Already clean." << CLR_RESET << "\n";
        }
        return true;
    }

    // =====================================================
    // Run (Build + Execute)
    // =====================================================

    bool BuildSystem::run(const std::string& spec_file) {
        auto workspace_opt = ConfigParser::parse(spec_file);
        if (!workspace_opt) return false;

        // Build first
        if (!build(spec_file)) return false;

        // Find the first APP project and execute it
        WorkspaceConfig ws = *workspace_opt;
        for (const auto& proj : ws.projects) {
            if (proj.type == ProjectType::APP) {
                fs::path spec_path = fs::absolute(spec_file);
                std::string workspace_root = spec_path.parent_path().string();
                fs::path project_dir = (fs::path(workspace_root) / proj.path).lexically_normal();
                fs::path binary = project_dir / proj.name;

                if (!fs::exists(binary)) {
                    std::cerr << CLR_RED << "Binary not found: " << binary << CLR_RESET << "\n";
                    return false;
                }

                std::cout << "\n" << CLR_BOLD << CLR_CYAN << "▶ Running " << proj.name << "..." << CLR_RESET << "\n\n";
                int result = std::system(binary.string().c_str());
                return result == 0;
            }
        }

        std::cerr << CLR_RED << "No executable project found in workspace." << CLR_RESET << "\n";
        return false;
    }

    // =====================================================
    // Publish (Build + Copy to Output Directory)
    // =====================================================

    bool BuildSystem::publish(const std::string& spec_file, const std::string& output_dir) {
        auto workspace_opt = ConfigParser::parse(spec_file);
        if (!workspace_opt) return false;

        fs::path spec_path = fs::absolute(spec_file);
        m_workspace_root = spec_path.parent_path().string();

        // Determine publish directory
        fs::path publish_dir;
        if (!output_dir.empty()) {
            publish_dir = fs::absolute(output_dir);
        } else {
            publish_dir = fs::path(m_workspace_root) / ".angara" / "publish";
        }

        std::cout << CLR_BOLD << CLR_MAGENTA << "Publishing Workspace: " << workspace_opt->name
                  << " v" << workspace_opt->version << CLR_RESET << "\n";

        // Step 1: Build the workspace
        if (!build(spec_file)) {
            std::cerr << CLR_RED << "Publish failed: build unsuccessful." << CLR_RESET << "\n";
            return false;
        }

        // Step 2: Re-parse to get project configs (workspace state already populated by build)
        std::cout << "\n" << CLR_BOLD << CLR_CYAN << "Determining projects to publish..." << CLR_RESET << "\n";

        fs::create_directories(publish_dir);

        int published_count = 0;

        // Collect all publishable targets from built artifacts
        for (const auto& proj : workspace_opt->projects) {
            fs::path project_dir = (fs::path(m_workspace_root) / proj.path).lexically_normal();
            fs::path artifact_path;

            if (proj.type == ProjectType::LIBRARY) {
                // Libraries: look for the shared lib in the project dir or build dir
                std::string lib_name = "lib" + proj.name + SO_EXT;
                fs::path in_project = project_dir / lib_name;
                fs::path in_build = fs::path(m_build_dir) / "modules" / lib_name;

                if (fs::exists(in_project)) {
                    artifact_path = in_project;
                } else if (fs::exists(in_build)) {
                    artifact_path = in_build;
                } else {
                    // Try without "lib" prefix
                    fs::path alt_name = project_dir / (proj.name + SO_EXT);
                    if (fs::exists(alt_name)) {
                        artifact_path = alt_name;
                    }
                }
            } else {
                // Apps: binary is in the project dir
                artifact_path = project_dir / proj.name;
            }

            if (!artifact_path.empty() && fs::exists(artifact_path)) {
                fs::path dest = publish_dir / artifact_path.filename();
                fs::copy_file(artifact_path, dest, fs::copy_options::overwrite_existing);

                std::cout << CLR_GREEN << "  " << proj.name << CLR_RESET
                          << CLR_GRAY << " → " << CLR_RESET
                          << CLR_BOLD << dest.string() << CLR_RESET << "\n";
                published_count++;
            } else {
                std::cout << CLR_YELLOW << "  " << proj.name
                          << " (no artifact found, skipping)" << CLR_RESET << "\n";
            }
        }

        // Also copy any native modules from the build directory
        fs::path local_mod_dir = fs::path(m_build_dir) / "modules";
        if (fs::exists(local_mod_dir)) {
            for (const auto& entry : fs::directory_iterator(local_mod_dir)) {
                if (entry.is_regular_file()) {
                    fs::path dest = publish_dir / entry.path().filename();
                    fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing);

                    std::cout << CLR_GREEN << "  " << entry.path().filename().string() << CLR_RESET
                              << CLR_GRAY << " → " << CLR_RESET
                              << CLR_BOLD << dest.string() << CLR_RESET
                              << CLR_GRAY << " (native module)" << CLR_RESET << "\n";
                    published_count++;
                }
            }
        }

        if (published_count == 0) {
            std::cerr << CLR_YELLOW << "No artifacts were published." << CLR_RESET << "\n";
            return false;
        }

        std::cout << "\n" << CLR_BOLD << CLR_GREEN << "Successfully published "
                  << published_count << " target(s) to '"
                  << publish_dir.string() << "'" << CLR_RESET << "\n";

        // Print the C# inspired easter egg
        print_publish_easter_egg();

        return true;
    }

    // =====================================================
    // Project Build Pipeline
    // =====================================================

    bool BuildSystem::build_project(const ProjectConfig& config,
                                    const std::map<std::string, std::string>& project_entries) {
        std::string project_root = m_project_dirs[config.name];

        // 1. Pre-build step
        if (!execute_build_step(config.pre_build, "pre-build")) {
            return false;
        }

        // 2. Build native modules declared in the project
        if (!build_native_modules(config, project_root)) {
            return false;
        }

        // 3. Compile Angara source
        CompilerDriver driver;
        driver.set_paths(m_std_lib_path, m_native_lib_path);
        driver.set_workspace_projects(project_entries);

        if (!m_target_triple.empty()) driver.set_target(m_target_triple);
        else if (!config.profile.target.empty()) driver.set_target(config.profile.target);
        if (!m_sysroot.empty()) driver.set_sysroot(m_sysroot);
        if (config.freestanding) driver.set_freestanding(true);
        if (config.nostdlib) driver.set_nostdlib(true);

        const std::string& entry_file = project_entries.at(config.name);

        std::cout << "    Compiling (LLVM)...\n";
        if (!driver.compile(config, entry_file)) {
            return false;
        }

        // 4. Link artifacts
        std::cout << "    Linking artifacts...\n";
        if (!link_artifacts(config,
                            driver.get_generated_object_files(),
                            driver.get_native_libs_linked(),
                            project_root)) {
            return false;
        }

        // Track the built artifact
        std::string output_dir = resolve_output_dir(config);
        fs::path bin_path = fs::path(output_dir) / config.name;
        if (fs::exists(bin_path)) {
            m_built_artifacts.push_back(bin_path.string());
        }

        // 5. Post-build step
        if (!execute_build_step(config.post_build, "post-build")) {
            return false;
        }

        return true;
    }

    // =====================================================
    // Native Module Compilation
    // =====================================================

    bool BuildSystem::build_native_modules(const ProjectConfig& config,
                                           const std::string& project_root) {
        if (config.native_modules.empty()) return true;

        std::cout << "    Building " << config.native_modules.size() << " native module(s)...\n";

        for (const auto& mod : config.native_modules) {
            if (!compile_native_module(mod, project_root)) {
                std::cerr << CLR_RED << "    !!! Failed to build native module: " << mod.name << CLR_RESET << "\n";
                return false;
            }
        }
        return true;
    }

    bool BuildSystem::compile_native_module(const NativeModuleConfig& mod,
                                            const std::string& project_root) {
        std::cout << CLR_BOLD << CLR_CYAN << "    [NM] " << CLR_RESET
                  << "Building native module: " << mod.name
                  << " (" << (mod.is_cpp ? "C++" : "C") << ", "
                  << mod.sources.size() << " sources)\n";

        // Determine compiler
        std::string compiler = mod.is_cpp ? "clang++" : "clang";

        // Resolve source paths relative to project root
        std::vector<fs::path> resolved_sources;
        for (const auto& src : mod.sources) {
            fs::path src_path = fs::path(project_root) / src;
            if (!fs::exists(src_path)) {
                std::cerr << CLR_RED << "    Source not found: " << src_path << CLR_RESET << "\n";
                return false;
            }
            resolved_sources.push_back(src_path);
        }

        // Compile each source to object file
        std::vector<std::string> object_files;
        fs::path obj_dir = fs::path(m_build_dir) / "obj" / mod.name;
        fs::create_directories(obj_dir);

        for (const auto& src_path : resolved_sources) {
            std::string base = src_path.stem().string();
            fs::path obj_path = obj_dir / (base + ".o");

            std::stringstream cmd;
            cmd << compiler << " -fPIC -Wall";
            if (mod.is_cpp) cmd << " -std=c++23";

            // Include directories
            cmd << " -I" << project_root;
            for (const auto& inc : mod.include_dirs) {
                cmd << " -I" << (fs::path(project_root) / inc).string();
            }

            // Extra cflags
            if (!mod.cflags.empty()) cmd << " " << mod.cflags;

            cmd << " -c " << src_path.string() << " -o " << obj_path.string();

            std::cout << CLR_GREEN << "      [CC] " << CLR_RESET << src_path.filename().string() << "\n";

            int result = std::system(cmd.str().c_str());
            if (result != 0) {
                std::cerr << CLR_RED << "    Compilation failed for " << src_path.filename().string() << CLR_RESET << "\n";
                return false;
            }
            object_files.push_back(obj_path.string());
        }

        // Link into shared library
        fs::path mod_output = fs::path(m_build_dir) / "modules" / (mod.name + SO_EXT);

        std::stringstream link_cmd;
        link_cmd << compiler << " -shared";
        for (const auto& obj : object_files) {
            link_cmd << " " << obj;
        }

        // Link libraries
        for (const auto& lib : mod.link_libs) {
            link_cmd << " -l" << lib;
        }

        // macOS frameworks
        for (const auto& fw : mod.link_frameworks) {
            link_cmd << " -framework " << fw;
        }

        // Extra ldflags
        if (!mod.ldflags.empty()) link_cmd << " " << mod.ldflags;

#if defined(__APPLE__)
        link_cmd << " -Wl,-install_name,@rpath/lib" << mod.name << SO_EXT;
#endif

        link_cmd << " -o " << mod_output.string();

        std::cout << CLR_MAGENTA << "      [MD] " << CLR_RESET << mod.name << SO_EXT << "\n";

        int result = std::system(link_cmd.str().c_str());
        if (result != 0) {
            std::cerr << CLR_RED << "    Linking failed for native module " << mod.name << CLR_RESET << "\n";
            return false;
        }

        std::cout << CLR_BOLD << CLR_GREEN << "    ✓ Built native module: " << mod.name << SO_EXT << CLR_RESET << "\n";
        return true;
    }

    // =====================================================
    // Artifact Linking
    // =====================================================

    bool BuildSystem::link_artifacts(const ProjectConfig& config,
                                     const std::set<std::string>& object_files,
                                     const std::vector<std::string>& discovered_libs,
                                     const std::string& project_root) const
    {
        std::string output_dir = resolve_output_dir(config);
        fs::path bin_path = fs::path(output_dir) / config.name;

        std::stringstream cmd;
        cmd << "clang";

        if (!m_target_triple.empty()) cmd << " -target " << m_target_triple;
        else if (!config.profile.target.empty()) cmd << " -target " << config.profile.target;
        if (!m_sysroot.empty()) cmd << " --sysroot " << m_sysroot;

        // Optimization flags from profile
        cmd << " " << resolve_opt_flags(config.profile);

        cmd << " -o " << bin_path.string();

        if (config.type == ProjectType::LIBRARY) {
            cmd << " -shared -fPIC";
        }

        // LLVM backend: runtime is embedded in the generated IR
        for (const auto& file : object_files) {
            cmd << " " << file;
        }

        cmd << " -I" << project_root;
        cmd << " -L" << m_native_lib_path;

        // Also search local build modules directory
        fs::path local_mod_dir = fs::path(m_build_dir) / "modules";
        if (fs::exists(local_mod_dir)) {
            cmd << " -L" << local_mod_dir.string();
        }

        // --- Smart Linking ---
        std::set<std::string> all_libs;
        for (const auto& lib : config.dependencies) all_libs.insert(lib);
        for (const auto& lib : discovered_libs) all_libs.insert(lib);

        // Add libraries from native modules
        for (const auto& mod : config.native_modules) {
            for (const auto& lib : mod.link_libs) all_libs.insert(lib);
        }

        for (const auto& lib : all_libs) {
            cmd << " -l" << lib;
        }

        // macOS frameworks from native modules
        for (const auto& mod : config.native_modules) {
            for (const auto& fw : mod.link_frameworks) {
                cmd << " -framework " << fw;
            }
        }

        // Extra profile ldflags
        if (!config.profile.ldflags.empty()) {
            cmd << " " << config.profile.ldflags;
        }

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
            cmd << " -nostdlib -Wno-return-type";
        } else {
            cmd << " -pthread -lm -Wno-return-type";
            cmd << " -Wl,-rpath," << m_native_lib_path;
            if (fs::exists(local_mod_dir)) {
                cmd << " -Wl,-rpath," << local_mod_dir.string();
            }
        }

        if (int result = std::system(cmd.str().c_str()); result != 0) {
            std::cerr << "    Linker failed for " << config.name << "\n";
            return false;
        }

        // Cleanup object files
        for (const auto& file : object_files) {
            fs::remove(file);
        }

        std::cout << "    " << CLR_BOLD << CLR_GREEN << "✓ Built " << bin_path.string() << CLR_RESET << "\n";
        return true;
    }
}