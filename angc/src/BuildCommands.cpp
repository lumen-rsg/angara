#include "CLI.h"
#include "BuildSystem.h"
#include "ProjectInitializer.h"
#include "PackageManager.h"
#include "Colors.h"
#include <iostream>
#include <filesystem>
namespace fs = std::filesystem;
namespace angara {

// TOOL-2: helper to configure a BuildSystem from CLI flags.
static void applyCliFlags(CLI& cli, BuildSystem& builder) {
    const auto& flags = cli.getFlags();
    if (flags.jobs > 0) builder.set_jobs(flags.jobs);
    if (flags.force_rebuild) builder.set_force_rebuild(true);
    if (flags.release) builder.set_build_mode(BuildMode::RELEASE);
    if (flags.debug) builder.set_build_mode(BuildMode::DEBUG);
}

int angara::CLI::handleNoArgs() {
    if (std::string project_file = find_local_project_file(); !project_file.empty()) {
        std::cout << CLR_BOLD << "Found project configuration: " << project_file << CLR_RESET << "\n";
        angara::BuildSystem builder;
        applyCliFlags(*this, builder);
        return builder.build(project_file) ? 0 : 1;
    }
    print_help();
    return 1;
}

int angara::CLI::handleInit(const std::vector<std::string>& args) {
    std::string template_name = (!args.empty()) ? args[0] : "";
    return angara::ProjectInitializer::run(template_name) ? 0 : 1;
}

int angara::CLI::handleRun(const std::vector<std::string>& args) {
    std::string project_file;
    if (!args.empty() && !args[0].empty() && args[0][0] != '-') {
        project_file = args[0];
    } else {
        project_file = find_local_project_file();
    }
    if (project_file.empty()) {
        std::cerr << CLR_RED << "[ERROR] No .abs project file found in the current directory." << CLR_RESET << "\n";
        return 1;
    }
    angara::BuildSystem builder;
    applyCliFlags(*this, builder);
    return builder.run(project_file) ? 0 : 1;
}

int angara::CLI::handleClean(const std::vector<std::string>& args) {
    std::string project_file;
    if (!args.empty() && !args[0].empty() && args[0][0] != '-') {
        project_file = args[0];
    } else {
        project_file = find_local_project_file();
    }
    if (project_file.empty()) {
        fs::path angara_dir = fs::absolute(".angara");
        if (fs::exists(angara_dir)) {
            std::cout << CLR_BOLD << CLR_RED << "[CL] " << CLR_RESET << "Cleaning .angara/\n";
            fs::remove_all(angara_dir);
            std::cout << CLR_BOLD << CLR_GREEN << "[OK] " << CLR_RESET << "Clean complete.\n";
            return 0;
        }
        std::cerr << CLR_YELLOW << "[WARN] No project file or .angara directory found." << CLR_RESET << "\n";
        return 1;
    }
    angara::BuildSystem builder;
    applyCliFlags(*this, builder);
    return builder.clean(project_file) ? 0 : 1;
}

int angara::CLI::handlePublish(const std::vector<std::string>& args) {
    std::string project_file;
    std::string publish_output;

    for (size_t i = 0; i < args.size(); ) {
        if ((args[i] == "-o" || args[i] == "--output") && i + 1 < args.size()) {
            publish_output = args[i + 1];
            i += 2;
        } else if (args[i][0] != '-') {
            project_file = args[i];
            ++i;
        } else {
            ++i;
        }
    }

    if (project_file.empty()) {
        project_file = find_local_project_file();
    }
    if (project_file.empty()) {
        std::cerr << CLR_RED << "[ERROR] No .abs project file found in the current directory." << CLR_RESET << "\n";
        return 1;
    }
    angara::BuildSystem builder;
    applyCliFlags(*this, builder);
    return builder.publish(project_file, publish_output) ? 0 : 1;
}

int angara::CLI::handlePackage(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << CLR_BOLD << "Usage:" << CLR_RESET << "\n";
        std::cerr << "  angc package install            Install dependencies (resolve + download)\n";
        std::cerr << "  angc package add <name>[@ver]   Add a dependency and update lockfile\n";
        std::cerr << "  angc package remove <name>      Remove a dependency and update lockfile\n";
        std::cerr << "  angc package update             Re-resolve all dependencies\n";
        std::cerr << "  angc package publish            Publish this package to the registry\n";
        return 1;
    }

    std::string subcmd = args[0];

    // Subcommands that need a .abs file
    auto find_abs = [&]() -> std::string {
        std::string f = find_local_project_file();
        if (f.empty()) {
            std::cerr << CLR_RED << "[ERROR] No .abs project file found in the current directory."
                      << CLR_RESET << "\n";
        }
        return f;
    };

    if (subcmd == "install") {
        std::string abs_path = find_abs();
        if (abs_path.empty()) return 1;

        fs::path project_dir = fs::path(abs_path).parent_path();
        auto ws = ConfigParser::parse(abs_path);
        if (!ws || ws->projects.empty()) {
            std::cerr << CLR_RED << "[ERROR] Could not parse project file." << CLR_RESET << "\n";
            return 1;
        }

        PackageManager pm(angara_home() + "/packages");
        auto resolved = pm.resolve_and_install(ws->projects[0].dependencies, project_dir.string());

        if (resolved.empty() && !ws->projects[0].dependencies.empty()) {
            std::cerr << CLR_YELLOW << "[WARN] No packages resolved. "
                      << "The registry may not yet be available." << CLR_RESET << "\n";
            return 0; // not an error — packages may be satisfied locally
        }

        std::cout << CLR_BOLD << CLR_GREEN << "[OK] " << CLR_RESET
                  << resolved.size() << " package(s) installed.\n";
        for (const auto& pkg : resolved) {
            std::cout << "  " << pkg.name << " v" << pkg.version.to_string() << "\n";
        }
        return 0;
    }

    if (subcmd == "add") {
        if (args.size() < 2) {
            std::cerr << CLR_RED << "Usage: angc package add <name>[@version]" << CLR_RESET << "\n";
            return 1;
        }
        std::string abs_path = find_abs();
        if (abs_path.empty()) return 1;

        std::string spec = args[1];
        std::string name = spec;
        std::string constraint;
        size_t at_pos = spec.find('@');
        if (at_pos != std::string::npos) {
            name = spec.substr(0, at_pos);
            constraint = spec.substr(at_pos + 1);
        }

        PackageManager pm(angara_home() + "/packages");
        return pm.add_dependency(abs_path, name, constraint) ? 0 : 1;
    }

    if (subcmd == "remove") {
        if (args.size() < 2) {
            std::cerr << CLR_RED << "Usage: angc package remove <name>" << CLR_RESET << "\n";
            return 1;
        }
        std::string abs_path = find_abs();
        if (abs_path.empty()) return 1;

        PackageManager pm(angara_home() + "/packages");
        return pm.remove_dependency(abs_path, args[1]) ? 0 : 1;
    }

    if (subcmd == "update") {
        std::string abs_path = find_abs();
        if (abs_path.empty()) return 1;

        PackageManager pm(angara_home() + "/packages");
        return pm.update_lockfile(abs_path) ? 0 : 1;
    }

    if (subcmd == "publish") {
        std::string abs_path = find_abs();
        if (abs_path.empty()) return 1;

        std::cout << CLR_BOLD << CLR_MAGENTA << "[PK] " << CLR_RESET
                  << "Publishing to registry...\n";
        std::cout << CLR_YELLOW << "[WARN] Registry publishing is not yet available.\n"
                  << "       Use 'angc publish' to create a local distribution instead."
                  << CLR_RESET << "\n";
        return 0;
    }

    std::cerr << CLR_RED << "Unknown package subcommand: " << subcmd << CLR_RESET << "\n";
    return 1;
}

} // namespace angara
