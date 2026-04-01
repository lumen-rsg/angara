#include "ProjectInitializer.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <sstream>

namespace fs = std::filesystem;

namespace angara {

    // Internal UI constants
    const auto C_RESET   = "\033[0m";
    const auto C_BOLD    = "\033[1m";
    const auto C_GREEN   = "\033[32m";
    const auto C_CYAN    = "\033[36m";
    const auto C_YELLOW  = "\033[33m";
    const auto C_MAGENTA = "\033[35m";

    std::string ProjectInitializer::prompt(const std::string& label, const std::string& defaultValue) {
        std::cout << C_BOLD << C_CYAN << "? " << C_RESET << label;
        if (!defaultValue.empty()) {
            std::cout << " (" << defaultValue << ")";
        }
        std::cout << ": ";

        std::string input;
        std::getline(std::cin, input);

        if (input.empty()) return defaultValue;
        return input;
    }

    bool ProjectInitializer::confirm(const std::string& label) {
        std::string res = prompt(label + " [y/N]", "n");
        std::transform(res.begin(), res.end(), res.begin(), ::tolower);
        return res == "y" || res == "yes";
    }

    bool ProjectInitializer::run() {
        std::cout << C_BOLD << C_MAGENTA << "--- Angara Project Initialization ---" << C_RESET << "\n";
        std::cout << "This will create a new workspace configuration (project.abs).\n\n";

        // 1. Workspace Configuration
        std::string ws_name = prompt("Workspace Name", fs::current_path().filename().string());
        std::string ws_author = prompt("Author Name", "user");
        std::string ws_version = prompt("Version", "0.1.0");

        std::stringstream abs_content;
        abs_content << "[workspace]\n";
        abs_content << "name = " << ws_name << "\n";
        abs_content << "author = " << ws_author << "\n";
        abs_content << "version = " << ws_version << "\n\n";

        abs_content << "<projects>\n\n";

        // 2. Project Loop
        bool adding = true;
        while (adding) {
            std::cout << "\n" << C_BOLD << C_YELLOW << ">> New Project Entry" << C_RESET << "\n";

            std::string p_name = prompt("  Project Name");
            if (p_name.empty()) {
                std::cout << "  Name cannot be empty. Skipping...\n";
                continue;
            }

            std::string p_type = prompt("  Type (app/library)", "app");
            std::string p_entry = prompt("  Entry File", "main.an");
            std::string p_deps = prompt("  Dependencies (comma separated, e.g: io, json)", "");

            abs_content << "[project]\n";
            abs_content << "name = " << p_name << "\n";
            abs_content << "type = " << p_type << "\n";
            abs_content << "entry = " << p_entry << "\n";

            // Format dependencies list
            if (p_deps.empty()) {
                abs_content << "dependencies = []\n\n";
            } else {
                abs_content << "dependencies = [" << p_deps << "]\n\n";
            }

            // Create directories and boilerplate
            try {
                fs::create_directories(p_name);
                std::string full_entry_path = p_name + "/" + p_entry;
                if (!fs::exists(full_entry_path)) {
                    std::ofstream entry_file(full_entry_path);
                    entry_file << "attach io;\n\nexport func main() -> i64 {\n    io.println(1, \"Hello from " << p_name << "!\");\n    return 0;\n}\n";
                }
            } catch (const std::exception& e) {
                std::cerr << "Warning: Could not create folders for " << p_name << ": " << e.what() << "\n";
            }

            adding = confirm("Add another project?");
        }

        abs_content << "<!projects>\n";

        // 3. Write .abs file
        std::ofstream outfile("project.abs");
        outfile << abs_content.str();
        outfile.close();

        std::cout << "\n" << C_BOLD << C_GREEN << "✓ Successfully initialized workspace in project.abs" << C_RESET << "\n";
        std::cout << "Run " << C_BOLD << "angc" << C_RESET << " to build your new projects.\n";

        return true;
    }
}