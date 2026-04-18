#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <thread>
#include <chrono>

#include "CompilerDriver.h"
#include "BuildSystem.h"
#include "ProjectInitializer.h"
#include "EasterEgg.h"
#include "../analyzer/printer/ASTPrinter.h"
#include "Lexer.h"
#include "Parser.h"
#include "ErrorHandler.h"
#include "Colors.h"

namespace fs = std::filesystem;

// --- Constants ---
const std::string ANGC_VERSION    = "3.1.0";
const std::string BACKEND_VERSION = "5.0.0";
const std::string ANGARA_SPEC     = "v3.1";

// --- Platform Detection ---
#if defined(__APPLE__)
    static const auto NATIVE_EXT = ".dylib";
#elif defined(__linux__)
    static const char* const NATIVE_EXT = ".so";
#elif defined(_WIN32)
    static const char* const NATIVE_EXT = ".dll";
#else
    static const char* const NATIVE_EXT = ".so";
#endif

// --- UI Helpers ---

void print_help() {
    std::cout << CLR_BOLD << "Usage:" << CLR_RESET << "\n";
    std::cout << "  angc                        Build the project in the current directory\n";
    std::cout << "  angc run                    Build and run the first app project\n";
    std::cout << "  angc clean                  Remove build artifacts\n";
    std::cout << "  angc publish                Build and copy targets to publish directory\n";
    std::cout << "  angc init                   Initialize a new project (interactive)\n";
    std::cout << "  angc init <template>        Initialize from a template (app, lib, embedded, gui)\n";
    std::cout << "  angc <file.an>              Compile a single source file\n";
    std::cout << "\n" << CLR_BOLD << "Commands:" << CLR_RESET << "\n";
    std::cout << "  run                         Build and execute the project\n";
    std::cout << "  clean                       Remove .angara/build directory\n";
    std::cout << "  publish                     Build and copy artifacts to a publish folder\n";
    std::cout << "  init                        Create a new project interactively\n";
    std::cout << "  modules                     List installed native modules\n";
    std::cout << "\n" << CLR_BOLD << "Options:" << CLR_RESET << "\n";
    std::cout << "  -v, --version               Show version information\n";
    std::cout << "  -h, --help                  Show this help message\n";
    std::cout << "  --path <project.abs>        Build a specific project configuration\n";
    std::cout << "  -o, --output <dir>          Output directory for publish command\n";
    std::cout << "  --release                   Build in release mode (opt level 2)\n";
    std::cout << "  --debug                     Build in debug mode (default, opt level 0)\n";
    std::cout << "  --dump-ast                  Debug: Print Abstract Syntax Tree\n";
    std::cout << "  --target <triple>           Cross-compile for target triple\n";
    std::cout << "  --sysroot <path>            Set sysroot for cross-compilation linker\n";
    std::cout << "  --freestanding              Freestanding mode (no libc, bare-metal)\n";
    std::cout << "  --nostdlib                  Don't link standard libraries\n";
    std::cout << std::endl;
}

void print_version() {
    std::cout << CLR_GREEN << CLR_BOLD << "angc" << CLR_RESET << ": Angara Compiler\n";
    std::cout << CLR_CYAN << "  • Compiler: " << CLR_RESET << ANGC_VERSION << "\n";
    std::cout << CLR_CYAN << "  • Backend:  " << CLR_RESET << BACKEND_VERSION << " (LLVM)" << "\n";
    std::cout << CLR_CYAN << "  • Spec:     " << CLR_RESET << ANGARA_SPEC << "\n";
    std::cout << CLR_GRAY << "  (c) 2026 Lumina Labs. This is a testing build." << CLR_RESET << "\n";
}

void list_modules() {
    const std::string mod_path = "/opt/angara/modules";
    std::cout << CLR_BOLD << "Installed Native Modules:" << CLR_RESET << "\n";
    std::cout << CLR_GRAY << "  Path: " << mod_path << CLR_RESET << "\n\n";

    int count = 0;
    try {
        for (const auto& entry : fs::directory_iterator(mod_path)) {
            if (!entry.is_regular_file()) continue;
            std::string name = entry.path().filename().string();
            // Strip "lib" prefix and extension
            if (name.rfind("lib") == 0) name = name.substr(3);
            auto ext = entry.path().extension().string();
            if (ext == ".dylib" || ext == ".so" || ext == ".dll") {
                name = name.substr(0, name.size() - ext.size());
                std::cout << CLR_GREEN << "  • " << CLR_RESET << name
                          << CLR_GRAY << " (" << entry.path().filename().string() << ")" << CLR_RESET << "\n";
                count++;
            }
        }
    } catch (...) {
        std::cout << CLR_YELLOW << "  (module directory not found)" << CLR_RESET << "\n";
    }

    if (count == 0) {
        std::cout << CLR_YELLOW << "  No modules installed." << CLR_RESET << "\n";
    }
    std::cout << "\n";
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

    const std::string os_suffix = get_host_os_triple_suffix();

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
        if (std::string project_file = find_local_project_file(); !project_file.empty()) {
            std::cout << CLR_BOLD << "Found project configuration: " << project_file << CLR_RESET << "\n";
            angara::BuildSystem builder;
            return builder.build(project_file) ? 0 : 1;
        } else {
            print_help();
            return 1;
        }
    }

    const std::string cmd = args[0];

    // --- Subcommands ---
    if (cmd == "init") {
        std::string template_name = (args.size() > 1) ? args[1] : "";
        return angara::ProjectInitializer::run(template_name) ? 0 : 1;
    }

    if (cmd == "run") {
        std::string project_file;
        if (args.size() > 1 && !args[1].empty() && args[1][0] != '-') {
            project_file = args[1];
        } else {
            project_file = find_local_project_file();
        }
        if (project_file.empty()) {
            std::cerr << CLR_RED << "No .abs project file found." << CLR_RESET << "\n";
            return 1;
        }
        angara::BuildSystem builder;
        return builder.run(project_file) ? 0 : 1;
    }

    if (cmd == "clean") {
        std::string project_file;
        if (args.size() > 1 && !args[1].empty() && args[1][0] != '-') {
            project_file = args[1];
        } else {
            project_file = find_local_project_file();
        }
        if (project_file.empty()) {
            // If no .abs file, just clean .angara/ if it exists
            fs::path angara_dir = fs::absolute(".angara");
            if (fs::exists(angara_dir)) {
                std::cout << CLR_BOLD << CLR_RED << "[CL] " << CLR_RESET << "Cleaning .angara/\n";
                fs::remove_all(angara_dir);
                std::cout << CLR_BOLD << CLR_GREEN << "✓ Clean complete." << CLR_RESET << "\n";
                return 0;
            }
            std::cerr << CLR_YELLOW << "No project file or .angara directory found." << CLR_RESET << "\n";
            return 1;
        }
        angara::BuildSystem builder;
        return builder.clean(project_file) ? 0 : 1;
    }

    if (cmd == "publish") {
        std::string project_file;
        std::string publish_output;

        // Parse publish-specific arguments
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
            std::cerr << CLR_RED << "No .abs project file found." << CLR_RESET << "\n";
            return 1;
        }
        angara::BuildSystem builder;
        return builder.publish(project_file, publish_output) ? 0 : 1;
    }

    if (cmd == "modules") {
        list_modules();
        return 0;
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
        angara::run_easter_egg();
        return 0;
    }

    // Parse global flags
    std::string resolved_target;
    std::string resolved_sysroot;
    bool dump_ast = false;
    bool flag_freestanding = false;
    bool flag_nostdlib = false;
    bool flag_release = false;
    // flag_debug: debug is the default, --debug is a no-op

    for (size_t i = 0; i < args.size(); ) {
        if (args[i] == "--target" && i + 1 < args.size()) {
            resolved_target = resolve_target_triple(args[i + 1]);
            args.erase(args.begin() + i, args.begin() + i + 2);
        } else if (args[i] == "--sysroot" && i + 1 < args.size()) {
            resolved_sysroot = args[i + 1];
            args.erase(args.begin() + i, args.begin() + i + 2);
        } else if (args[i] == "--dump-ast") {
            dump_ast = true;
            args.erase(args.begin() + i);
        } else if (args[i] == "--freestanding") {
            flag_freestanding = true;
            args.erase(args.begin() + i);
        } else if (args[i] == "--nostdlib") {
            flag_nostdlib = true;
            args.erase(args.begin() + i);
        } else if (args[i] == "--release") {
            flag_release = true;
            args.erase(args.begin() + i);
        } else if (args[i] == "--debug") {
            args.erase(args.begin() + i);
        } else {
            ++i;
        }
    }

    // Handle --dump-ast: lex + parse the given file, print AST, exit
    if (dump_ast) {
        if (args.empty() || args[0].length() < 3 || args[0].substr(args[0].length() - 3) != ".an") {
            std::cerr << CLR_RED << "Error: --dump-ast requires a .an source file." << CLR_RESET << "\n";
            return 1;
        }
        const std::string& file = args[0];
        std::string source = angara::CompilerDriver::read_file(file);
        if (source.empty()) {
            std::cerr << CLR_RED << "Error: Could not read file '" << file << "'." << CLR_RESET << "\n";
            return 1;
        }
        auto filename_ptr = std::make_shared<std::string>(file);
        angara::ErrorHandler errorHandler(source);
        angara::Lexer lexer(source, filename_ptr, errorHandler);
        auto tokens = lexer.scanTokens();
        if (errorHandler.hadError()) { errorHandler.printSummary(); return 1; }

        angara::Parser parser(tokens, errorHandler);
        auto statements = parser.parseStmts();
        if (errorHandler.hadError()) { errorHandler.printSummary(); return 1; }

        angara::ASTPrinter printer;
        printer.print(statements);
        return 0;
    }

    // Handle Explicit Path Build
    if (cmd == "--path") {
        if (args.size() < 2) {
            std::cerr << CLR_RED << "Error: --path requires a filename." << CLR_RESET << "\n";
            return 1;
        }
        angara::BuildSystem builder;
        if (!resolved_target.empty()) builder.set_target(resolved_target);
        if (!resolved_sysroot.empty()) builder.set_sysroot(resolved_sysroot);
        if (flag_release) builder.set_build_mode(angara::BuildMode::RELEASE);
        return builder.build(args[1]) ? 0 : 1;
    }

    // Handle Single File Compilation
    // Check remaining args (after flag stripping) for .an file
    if (!args.empty() && args[0].length() >= 3 && args[0].substr(args[0].length() - 3) == ".an") {
        const std::string& source_file = args[0];
        std::string base_name = angara::CompilerDriver::get_base_name(source_file);

        angara::ProjectConfig legacy_config;
        legacy_config.name = base_name;
        legacy_config.entry_point = source_file;
        legacy_config.path = ".";
        legacy_config.type = angara::ProjectType::APP;
        legacy_config.freestanding = flag_freestanding;
        legacy_config.nostdlib = flag_nostdlib;

        if (flag_release) {
            legacy_config.profile.mode = angara::BuildMode::RELEASE;
            legacy_config.profile.opt_level = 2;
        }

        angara::CompilerDriver driver;
        if (!resolved_target.empty()) driver.set_target(resolved_target);
        if (!resolved_sysroot.empty()) driver.set_sysroot(resolved_sysroot);
        if (flag_freestanding) driver.set_freestanding(true);
        if (flag_nostdlib) driver.set_nostdlib(true);
        // Search locally-built modules first, then installed ones
        std::string native_mod_path = "/opt/angara/modules";
        if (fs::exists("build/modules")) {
            native_mod_path = fs::absolute("build/modules").string();
        }
        driver.set_paths("/opt/angara/src/modules", native_mod_path);

        if (driver.compile(legacy_config, source_file)) {
            std::cout << CLR_GREEN << "LLVM codegen complete." << CLR_RESET << " Linking..." << std::endl;

            // Build the link command (LLVM backend is self-contained)
            std::stringstream cmd_link;
            cmd_link << "clang";
            if (!resolved_target.empty()) cmd_link << " -target " << resolved_target;
            if (!resolved_sysroot.empty()) cmd_link << " --sysroot " << resolved_sysroot;

            // Optimization
            if (flag_release) cmd_link << " -O2";
            else cmd_link << " -O0";

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
                std::string local_mod = (fs::path("build/modules") / (lib + NATIVE_EXT)).string();
                std::string installed_mod = "/opt/angara/modules/" + lib + NATIVE_EXT;
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

            // Freestanding: skip host linker, emit object file for bare-metal toolchain
            if (flag_freestanding) {
                std::string obj_output = base_name + ".o";
                const auto& objs = driver.get_generated_object_files();
                if (objs.size() == 1) {
                    fs::rename(*objs.begin(), obj_output);
                } else {
                    obj_output = *objs.begin();
                }
                std::cout << CLR_BOLD << CLR_GREEN << "Freestanding object emitted: " << obj_output << CLR_RESET << "\n";
                std::cout << CLR_CYAN << "  Link with your bare-metal toolchain, e.g.:" << CLR_RESET << "\n";
                std::cout << CLR_GRAY << "  aarch64-unknown-none-elf-gcc -nostdlib -T linker.ld -o kernel "
                          << obj_output << CLR_RESET << "\n";
                return 0;
            }

            // Standard flags
            if (flag_nostdlib) {
#if defined(__APPLE__)
                cmd_link << " -nodefaultlibs -lSystem -Wno-return-type";
#else
                cmd_link << " -nostdlib -Wno-return-type";
#endif
            } else {
                cmd_link << " -pthread -lm -Wno-return-type";
                cmd_link << " -Wl,-rpath,/opt/angara/modules";
            }

            int res = system(cmd_link.str().c_str());
            if (res == 0) {
                std::cout << CLR_BOLD << CLR_GREEN << "Successfully built: " << base_name << CLR_RESET << "\n";

                // Cleanup generated object files
                for (const auto& o_file : driver.get_generated_object_files()) {
                    remove(o_file.c_str());
                }
                return 0;
            } else {
                std::cerr << CLR_RED << "Linker failed." << CLR_RESET << "\n";
                return 1;
            }
        }
        return 1;
    }

    // Unknown Command
    std::cerr << CLR_RED << "Unknown argument: " << cmd << CLR_RESET << "\n";
    print_help();
    return 1;
}