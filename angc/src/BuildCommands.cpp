#include "CLI.h"
#include "BuildSystem.h"
#include "ProjectInitializer.h"
#include "Colors.h"
#include <iostream>
#include <filesystem>
namespace fs = std::filesystem;
namespace angara {

int angara::CLI::handleNoArgs() {
    if (std::string project_file = find_local_project_file(); !project_file.empty()) {
        std::cout << CLR_BOLD << "Found project configuration: " << project_file << CLR_RESET << "\n";
        angara::BuildSystem builder;
        return builder.build(project_file) ? 0 : 1;
    }
    print_help();
    return 1;
}

int angara::CLI::handleInit(const std::vector<std::string>& args) {
    std::string template_name = (args.size() > 1) ? args[1] : "";
    return angara::ProjectInitializer::run(template_name) ? 0 : 1;
}

int angara::CLI::handleRun(const std::vector<std::string>& args) {
    std::string project_file;
    if (args.size() > 1 && !args[1].empty() && args[1][0] != '-') {
        project_file = args[1];
    } else {
        project_file = find_local_project_file();
    }
    if (project_file.empty()) {
        std::cerr << CLR_RED << "[ERROR] No .abs project file found in the current directory." << CLR_RESET << "\n";
        return 1;
    }
    angara::BuildSystem builder;
    return builder.run(project_file) ? 0 : 1;
}

int angara::CLI::handleClean(const std::vector<std::string>& args) {
    std::string project_file;
    if (args.size() > 1 && !args[1].empty() && args[1][0] != '-') {
        project_file = args[1];
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
    return builder.clean(project_file) ? 0 : 1;
}

int angara::CLI::handlePublish(const std::vector<std::string>& args) {
    std::string project_file;
    std::string publish_output;

    for (size_t i = 1; i < args.size(); ) {
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
    return builder.publish(project_file, publish_output) ? 0 : 1;
}
} // namespace angara
