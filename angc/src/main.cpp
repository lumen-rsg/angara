#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <thread>
#include <chrono>
#include <random>

#include "CompilerDriver.h"
#include "BuildSystem.h"
#include "ProjectInitializer.h"

namespace fs = std::filesystem;

// --- Constants ---
const std::string ANGC_VERSION    = "2.7.1-beta";
const std::string BACKEND_VERSION = "3.1.1";
const std::string ANGARA_SPEC     = "v3.0-draft";

// --- Colors ---
const auto RESET   = "\033[0m";
const auto BOLD    = "\033[1m";
const auto RED     = "\033[31m";
const auto GREEN   = "\033[32m";
const auto MAGENTA = "\033[35m";
const auto CYAN    = "\033[36m";
const auto GRAY    = "\033[90m";

// --- UI Helpers ---

void print_typing(const std::string& text, int delay_ms = 30) {
    for (char c : text) {
        std::cout << c << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }
    std::cout << std::endl;
}

void print_help() {
    std::cout << BOLD << "Usage:" << RESET << "\n";
    std::cout << "  angc                        Build the project in the current directory (.abs file)\n";
    std::cout << "  angc --path <project.abs>   Build a specific project configuration\n";
    std::cout << "  angc <file.an>              Compile a single source file (Legacy Mode)\n";
    std::cout << "\n" << BOLD << "Options:" << RESET << "\n";
    std::cout << "  -v, --version               Show version information\n";
    std::cout << "  -h, --help                  Show this help message\n";
    std::cout << "  --dump-ast                  Debug: Print Abstract Syntax Tree\n";
    std::cout << std::endl;
}

void print_version() {
    std::cout << GREEN << BOLD << "angc" << RESET << ": Angara Compiler\n";
    std::cout << CYAN << "  • Compiler: " << RESET << ANGC_VERSION << "\n";
    std::cout << CYAN << "  • Backend:  " << RESET << BACKEND_VERSION << "\n";
    std::cout << CYAN << "  • Spec:     " << RESET << ANGARA_SPEC << "\n";
    std::cout << GRAY << "  (c) 2026 Lumina Labs. This is a testing build." << RESET << "\n";
}

void run_easter_egg() {
    std::vector<std::string> startup_seq = {
        "You think you can just ask this...",
        "Fine, you can.",
        "Allocating infinite memory buffers...",
        "Synchronizing with lumina rays...",
        "Compiling perfection..."
    };

    std::cout << "\n";
    for (const auto& line : startup_seq) {
        std::cout << GRAY << "  [SYS] " << line << RESET << "\r";
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        // Clear line
        std::cout << "\033[2K";
    }

    std::cout << "\033[2K"; // Clear line
    print_typing(BOLD + std::string(CYAN) + "-> System Online." + RESET, 50);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    std::cout << MAGENTA << R"(
    ___    _   __  ______   ___     ____     ___
   /   |  / | / / / ____/  /   |   / __ \   /   |
  / /| | /  |/ / / / __   / /| |  / /_/ /  / /| |
 / ___ |/ /|  / / /_/ /  / ___ | / _, _/  / ___ |
/_/  |_/_/ |_/  \____/  /_/  |_|/_/ |_|  /_/  |_|
)" << RESET << "\n";

    std::cout << "    " << BOLD << "The craft of code is the craft of thought." << RESET << "\n\n";
}

// --- Logic Helpers ---

std::string find_local_project_file() {
    try {
        for (const auto& entry : fs::directory_iterator(".")) {
            if (entry.is_regular_file() && entry.path().extension() == ".abs") {
                return entry.path().string();
            }
        }
    } catch (...) {
        // Ignore filesystem errors
    }
    return "";
}

// --- Entry Point ---

int main(int argc, char* argv[]) {
    // 1. Argument Pre-processing
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);

    // 2. Handle No Arguments (Implicit Build)
    if (args.empty()) {
        std::string project_file = find_local_project_file();
        if (!project_file.empty()) {
            std::cout << BOLD << "Found project configuration: " << project_file << RESET << "\n";
            angara::BuildSystem builder;
            return builder.build(project_file) ? 0 : 1;
        } else {
            // No project found, print help
            print_help();
            return 1;
        }
    }

    const std::string& cmd = args[0];

    if (cmd == "init") {
        return angara::ProjectInitializer::run() ? 0 : 1;
    }

    // 3. Handle Flags
    if (cmd == "-v" || cmd == "--version") {
        print_version();
        return 0;
    }

    if (cmd == "-h" || cmd == "--help") {
        print_help();
        return 0;
    }

    if (cmd == "--make-perfect") {
        run_easter_egg();
        return 0;
    }

    // 4. Handle Explicit Path Build
    if (cmd == "--path") {
        if (args.size() < 2) {
            std::cerr << RED << "Error: --path requires a filename." << RESET << "\n";
            return 1;
        }
        angara::BuildSystem builder;
        return builder.build(args[1]) ? 0 : 1;
    }

    // 5. Handle Legacy/Single File Compilation
    // If it ends in .an, treat it as a source file.
    if (cmd.length() >= 3 && cmd.substr(cmd.length() - 3) == ".an") {
        std::string base_name = angara::CompilerDriver::get_base_name(cmd);

        // --- Create an on-the-fly ProjectConfig ---
        angara::ProjectConfig legacy_config;
        legacy_config.name = base_name;
        legacy_config.entry_point = cmd;
        legacy_config.path = "."; // Current directory
        legacy_config.type = angara::ProjectType::APP;

        angara::CompilerDriver driver;
        // Use the standard installation paths
        driver.set_paths("/opt/angara/src/modules", "/opt/angara/modules");

        if (driver.compile(legacy_config, cmd)) {
            std::cout << GREEN << "Transpilation complete." << RESET << " Linking..." << std::endl;

            // 1. Build the Link Command
            std::stringstream cmd_link;
            cmd_link << "clang -o " << base_name;

            // 2. Add all generated C files
            // (The driver might have generated multiple C files if 'cmd' attached other local files)
            for (const auto& c_file : driver.get_generated_c_files()) {
                cmd_link << " " << c_file;
            }

            // 3. Add Runtime implementation
            cmd_link << " /opt/angara/src/runtime/angara_runtime.c";

            // 4. Set Search Paths
            // Include current dir, runtime dir, and stdlib dir
            cmd_link << " -I. -I/opt/angara/src/runtime -I/opt/angara/src/modules";
            cmd_link << " -L/opt/angara/modules";

            // 5. Add Native Dependencies
            // Deduplicate using a set
            std::set<std::string> libs;
            for (const auto& lib : driver.get_native_libs_linked()) {
                libs.insert(lib);
            }
            for (const auto& lib : libs) {
                cmd_link << " -l" << lib;
            }

            // 6. Standard Flags
            cmd_link << " -pthread -lm -O2 -Wno-return-type";
            cmd_link << " -Wl,-rpath,/opt/angara/modules";

            // 7. Execute Linker
            int res = system(cmd_link.str().c_str());
            if (res == 0) {
                std::cout << BOLD << GREEN << "Successfully built: " << base_name << RESET << "\n";

                // Cleanup generated C/H artifacts
                for (const auto& c_file : driver.get_generated_c_files()) {
                    remove(c_file.c_str());
                    // Deduce and remove .h
                    std::string h_file = c_file.substr(0, c_file.find_last_of('.')) + ".h";
                    remove(h_file.c_str());
                }
                return 0;
            } else {
                std::cerr << RED << "Linker failed." << RESET << "\n";
                return 1;
            }
        }
        return 1;
    }

    // 6. Unknown Command
    std::cerr << RED << "Unknown argument: " << cmd << RESET << "\n";
    print_help();
    return 1;
}