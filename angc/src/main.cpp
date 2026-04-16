#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <thread>
#include <chrono>

#include "CompilerDriver.h"
#include "BuildSystem.h"
#include "ProjectInitializer.h"

namespace fs = std::filesystem;

// --- Constants ---
const std::string ANGC_VERSION    = "3.0.0";
const std::string BACKEND_VERSION = "5.0.0";
const std::string ANGARA_SPEC     = "v3.0";

// --- Colors ---
const auto RESET   = "\033[0m";
const auto BOLD    = "\033[1m";
const auto RED     = "\033[31m";
const auto GREEN   = "\033[32m";
const auto YELLOW  = "\033[33m";
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
    std::cout << "  angc <file.an>              Compile a single source file\n";
    std::cout << "\n" << BOLD << "Options:" << RESET << "\n";
    std::cout << "  -v, --version               Show version information\n";
    std::cout << "  -h, --help                  Show this help message\n";
    std::cout << "  --dump-ast                  Debug: Print Abstract Syntax Tree\n";
    std::cout << "  --target <triple>           Cross-compile for target triple\n";
    std::cout << "  --sysroot <path>            Set sysroot for cross-compilation linker\n";
    std::cout << std::endl;
}

void print_version() {
    std::cout << GREEN << BOLD << "angc" << RESET << ": Angara Compiler\n";
    std::cout << CYAN << "  • Compiler: " << RESET << ANGC_VERSION << "\n";
    std::cout << CYAN << "  • Backend:  " << RESET << BACKEND_VERSION << " (LLVM)" << "\n";
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
        std::cout << "\033[2K";
    }

    std::cout << "\033[2K";
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

// --- Target Triple Resolution ---

static std::string get_host_os_triple_suffix() {
#if defined(__APPLE__)
    return "-apple-darwin";
#elif defined(__linux__)
    return "-unknown-linux-gnu";
#elif defined(_WIN32)
    return "-pc-windows-msvc";
#else
    return "-unknown-elf";
#endif
}

static std::string resolve_target_triple(const std::string& input) {
    if (input.find('-') != std::string::npos) {
        return input;
    }

    std::string os_suffix = get_host_os_triple_suffix();

    if (input == "arm64" || input == "aarch64") {
        return "aarch64" + os_suffix;
    }
    if (input == "amd64" || input == "x86_64" || input == "x64") {
        return "x86_64" + os_suffix;
    }
    if (input == "riscv64") {
        return "riscv64" + os_suffix;
    }
    if (input == "wasm32") {
        return "wasm32-unknown-unknown";
    }
    if (input == "wasm64") {
        return "wasm64-unknown-unknown";
    }

    return input;
}

// --- Logic Helpers ---

std::string find_local_project_file() {
    try {
        for (const auto& entry : fs::directory_iterator(".")) {
            if (entry.is_regular_file() && entry.path().extension() == ".abs") {
                return entry.path().string();
            }
        }
    } catch (...) {}
    return "";
}

// --- Entry Point ---

int main(int argc, char* argv[]) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);

    // Handle No Arguments (Implicit Build)
    if (args.empty()) {
        std::string project_file = find_local_project_file();
        if (!project_file.empty()) {
            std::cout << BOLD << "Found project configuration: " << project_file << RESET << "\n";
            angara::BuildSystem builder;
            return builder.build(project_file) ? 0 : 1;
        } else {
            print_help();
            return 1;
        }
    }

    const std::string& cmd = args[0];

    if (cmd == "init") {
        return angara::ProjectInitializer::run() ? 0 : 1;
    }

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

    // Parse --target and --sysroot flags
    std::string resolved_target;
    std::string resolved_sysroot;

    for (size_t i = 0; i < args.size(); ) {
        if (args[i] == "--target" && i + 1 < args.size()) {
            resolved_target = resolve_target_triple(args[i + 1]);
            args.erase(args.begin() + i, args.begin() + i + 2);
        } else if (args[i] == "--sysroot" && i + 1 < args.size()) {
            resolved_sysroot = args[i + 1];
            args.erase(args.begin() + i, args.begin() + i + 2);
        } else {
            ++i;
        }
    }

    // Handle Explicit Path Build
    if (cmd == "--path") {
        if (args.size() < 2) {
            std::cerr << RED << "Error: --path requires a filename." << RESET << "\n";
            return 1;
        }
        angara::BuildSystem builder;
        if (!resolved_target.empty()) builder.set_target(resolved_target);
        if (!resolved_sysroot.empty()) builder.set_sysroot(resolved_sysroot);
        return builder.build(args[1]) ? 0 : 1;
    }

    // Handle Single File Compilation
    if (cmd.length() >= 3 && cmd.substr(cmd.length() - 3) == ".an") {
        std::string base_name = angara::CompilerDriver::get_base_name(cmd);

        angara::ProjectConfig legacy_config;
        legacy_config.name = base_name;
        legacy_config.entry_point = cmd;
        legacy_config.path = ".";
        legacy_config.type = angara::ProjectType::APP;

        angara::CompilerDriver driver;
        if (!resolved_target.empty()) driver.set_target(resolved_target);
        if (!resolved_sysroot.empty()) driver.set_sysroot(resolved_sysroot);
        driver.set_paths("/opt/angara/src/modules", "/opt/angara/modules");

        if (driver.compile(legacy_config, cmd)) {
            std::cout << GREEN << "LLVM codegen complete." << RESET << " Linking..." << std::endl;

            // Build the link command (LLVM backend is self-contained)
            std::stringstream cmd_link;
            cmd_link << "clang";
            if (!resolved_target.empty()) cmd_link << " -target " << resolved_target;
            if (!resolved_sysroot.empty()) cmd_link << " --sysroot " << resolved_sysroot;
            cmd_link << " -o " << base_name;

            // Add all generated object files
            for (const auto& o_file : driver.get_generated_object_files()) {
                cmd_link << " " << o_file;
            }

            // Add native module dependencies
            std::set<std::string> libs;
            for (const auto& lib : driver.get_native_libs_linked()) {
                libs.insert(lib);
            }
            for (const auto& lib : libs) {
                std::string mod_path;
                std::string local_mod = (fs::path("../build/modules") / (lib + ".dylib")).string();
                std::string installed_mod = "/opt/angara/modules/" + lib + ".dylib";
                if (fs::exists(local_mod)) {
                    mod_path = fs::absolute(local_mod).string();
                } else if (fs::exists(installed_mod)) {
                    mod_path = installed_mod;
                } else {
                    cmd_link << " -l" << lib;
                    continue;
                }
                cmd_link << " " << mod_path;
            }

            // Standard flags
            cmd_link << " -pthread -lm -O2 -Wno-return-type";
            cmd_link << " -Wl,-rpath,/opt/angara/modules";

            int res = system(cmd_link.str().c_str());
            if (res == 0) {
                std::cout << BOLD << GREEN << "Successfully built: " << base_name << RESET << "\n";

                // Cleanup generated object files
                for (const auto& o_file : driver.get_generated_object_files()) {
                    remove(o_file.c_str());
                }
                return 0;
            } else {
                std::cerr << RED << "Linker failed." << RESET << "\n";
                return 1;
            }
        }
        return 1;
    }

    // Unknown Command
    std::cerr << RED << "Unknown argument: " << cmd << RESET << "\n";
    print_help();
    return 1;
}