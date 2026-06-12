#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <thread>
#include <chrono>
#include <set>
#include <sstream>
#include <fstream>

#include "CompilerDriver.h"
#include "BuildSystem.h"
#include "ProjectInitializer.h"
#include "EasterEgg.h"
#include "../analyzer/printer/ASTPrinter.h"
#include "../includes/Formatter.h"
#include "../includes/LSPServer.h"
#include "../includes/REPL.h"
#include "Lexer.h"
#include "Parser.h"
#include "ErrorHandler.h"
#include "Colors.h"
#include "StringUtils.h"
#include "Platform.h"

#include <llvm/Config/llvm-config.h>

namespace fs = std::filesystem;

const std::string ANGC_VERSION    = "3.1.0";
const std::string BACKEND_VERSION = "5.0.0";
const std::string ANGARA_SPEC     = "v3.1";

static constexpr const char* NATIVE_EXT = ANGARA_NATIVE_EXT;

static bool g_verbose = false;

static void verbose(const std::string& msg) {
    if (!g_verbose) return;
    std::cout << CLR_DIM << "  " << msg << CLR_RESET << "\n";
}

struct CliFlags {
    std::string target;
    std::string sysroot;
    std::string output_name;
    std::vector<std::string> link_files;
    bool dump_ast = false;
    bool dump_ir = false;
    bool emit_llvm = false;
    bool freestanding = false;
    bool nostdlib = false;
    bool release = false;
    bool debug = false;
    bool verbose_flag = false;
    bool wall = false;
    bool werror = false;
    std::vector<std::string> suppress_warnings;
    std::string error_format = "text";
    std::string gc = "chaperone";

    static CliFlags parse(std::vector<std::string>& args) {
        CliFlags flags;
        for (size_t i = 0; i < args.size(); ) {
            if (args[i] == "--target" && i + 1 < args.size()) {
                flags.target = args[i + 1];
                args.erase(args.begin() + i, args.begin() + i + 2);
            } else if (args[i] == "--sysroot" && i + 1 < args.size()) {
                flags.sysroot = args[i + 1];
                args.erase(args.begin() + i, args.begin() + i + 2);
            } else if ((args[i] == "-o" || args[i] == "--output") && i + 1 < args.size()) {
                flags.output_name = args[i + 1];
                args.erase(args.begin() + i, args.begin() + i + 2);
            } else if ((args[i] == "-l" || args[i] == "--link") && i + 1 < args.size()) {
                flags.link_files.push_back(args[i + 1]);
                args.erase(args.begin() + i, args.begin() + i + 2);
            } else if (args[i] == "--dump-ast") {
                flags.dump_ast = true;
                args.erase(args.begin() + i);
            } else if (args[i] == "--dump-ir") {
                flags.dump_ir = true;
                args.erase(args.begin() + i);
            } else if (args[i] == "--emit-llvm") {
                flags.emit_llvm = true;
                args.erase(args.begin() + i);
            } else if (args[i] == "--freestanding") {
                flags.freestanding = true;
                args.erase(args.begin() + i);
            } else if (args[i] == "--nostdlib") {
                flags.nostdlib = true;
                args.erase(args.begin() + i);
            } else if (args[i] == "--release") {
                flags.release = true;
                args.erase(args.begin() + i);
            } else if (args[i] == "-V" || args[i] == "--verbose") {
                flags.verbose_flag = true;
                g_verbose = true;
                args.erase(args.begin() + i);
            } else if (args[i] == "--debug") {
                flags.debug = true;
                args.erase(args.begin() + i);
            } else if (args[i] == "-Wall") {
                flags.wall = true;
                args.erase(args.begin() + i);
            } else if (args[i] == "-Werror") {
                flags.werror = true;
                args.erase(args.begin() + i);
            } else if (args[i].substr(0, 5) == "-Wno-") {
                flags.suppress_warnings.push_back(args[i].substr(5));
                args.erase(args.begin() + i);
            } else if (args[i] == "--error-format" && i + 1 < args.size()) {
                flags.error_format = args[i + 1];
                args.erase(args.begin() + i, args.begin() + i + 2);
            } else if (args[i] == "--gc" && i + 1 < args.size()) {
                flags.gc = args[i + 1];
                args.erase(args.begin() + i, args.begin() + i + 2);
            } else {
                ++i;
            }
        }
        return flags;
    }
};

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

static std::string find_local_project_file() {
    try {
        for (const auto& entry : fs::directory_iterator(".")) {
            if (entry.is_regular_file() && entry.path().extension() == ".abs") {
                return entry.path().string();
            }
        }
    } catch (const std::exception& e) {
        verbose("find_local_project_file: " + std::string(e.what()));
    } catch (...) {
        verbose("find_local_project_file: unknown exception");
    }
    return "";
}

static bool is_an_file(const std::string& s) {
    return s.length() >= 3 && s.substr(s.length() - 3) == ".an";
}

static void print_help() {
    std::cout << CLR_BOLD << "Usage:" << CLR_RESET << "\n";
    std::cout << "  angc                        Build the project in the current directory\n";
    std::cout << "  angc run                    Build and run the first app project\n";
    std::cout << "  angc clean                  Remove build artifacts\n";
    std::cout << "  angc publish                Build and copy targets to publish directory\n";
    std::cout << "  angc init                   Initialize a new project (interactive)\n";
    std::cout << "  angc init <template>        Initialize from a template (app, lib, embedded, gui)\n";
    std::cout << "  angc check <file.an>        Lex, parse, and typecheck (no codegen)\n";
    std::cout << "  angc <file.an>              Compile a single source file\n";
    std::cout << "\n" << CLR_BOLD << "Commands:" << CLR_RESET << "\n";
    std::cout << "  run       Build and execute the project\n";
    std::cout << "  check     Lex, parse, and typecheck (no codegen)\n";
    std::cout << "  test      Run tests from tests/ directory\n";
    std::cout << "  clean     Remove .angara/build directory\n";
    std::cout << "  publish   Build and copy artifacts to a publish folder\n";
    std::cout << "  init      Create a new project interactively\n";
    std::cout << "  modules   List installed native modules\n";
    std::cout << "  fmt       Format source files (-w to write in place)\n";
    std::cout << "  watch     Watch for file changes and rebuild\n";
    std::cout << "  explain   Explain a compiler error or warning code\n";
    std::cout << "  lsp       Start Language Server Protocol server\n";
    std::cout << "  repl      Start interactive read-eval-print loop\n";
    std::cout << "\n" << CLR_BOLD << "Options:" << CLR_RESET << "\n";
    std::cout << "  -v, --version               Show version information\n";
    std::cout << "  -h, --help                  Show this help message\n";
    std::cout << "  -V, --verbose               Show extra diagnostic output\n";
    std::cout << "  --path <project.abs>        Build a specific project configuration\n";
    std::cout << "  -o, --output <path>         Output binary name or publish directory\n";
    std::cout << "  -l, --link <file>           Link additional object or library file\n";
    std::cout << "  --release                   Build in release mode (opt level 2)\n";
    std::cout << "  --debug                     Build in debug mode (default, opt level 0)\n";
    std::cout << "  -Wall                       Enable all warnings\n";
    std::cout << "  -Werror                     Treat warnings as errors\n";
    std::cout << "  -Wno-XXX                    Suppress specific warning (e.g., -Wno-W003)\n";
    std::cout << "  --error-format <text|json>  Set diagnostic output format (default: text)\n";
    std::cout << "  --dump-ast                  Debug: Print Abstract Syntax Tree\n";
    std::cout << "  --dump-ir                   Debug: Emit unoptimized LLVM IR (.ll)\n";
    std::cout << "  --emit-llvm                 Emit LLVM IR to stdout instead of compiling\n";
    std::cout << "  --target <triple>           Cross-compile for target triple\n";
    std::cout << "  --sysroot <path>            Set sysroot for cross-compilation linker\n";
    std::cout << "  --freestanding              Freestanding mode (no libc, bare-metal)\n";
    std::cout << "  --nostdlib                  Don't link standard libraries\n";
    std::cout << "  --gc <chaperone|mark-sweep> Select garbage collector (default: chaperone)\n";
    std::cout << std::endl;
}

static void print_version() {
    std::cout << CLR_GREEN << CLR_BOLD << "angc" << CLR_RESET << ": Angara Compiler\n";
    std::cout << CLR_CYAN << "  • Compiler: " << CLR_RESET << ANGC_VERSION << "\n";
    std::cout << CLR_CYAN << "  • Backend:  " << CLR_RESET << BACKEND_VERSION << " (LLVM " << LLVM_VERSION_MAJOR << "." << LLVM_VERSION_MINOR << "." << LLVM_VERSION_PATCH << ")\n";
    std::cout << CLR_CYAN << "  • Spec:     " << CLR_RESET << ANGARA_SPEC << "\n";
    std::cout << CLR_GRAY << "  (c) 2026 Lumina Labs. This is a testing build." << CLR_RESET << "\n";
}

static void list_modules() {
    const std::string mod_path = "/opt/angara/modules";
    std::cout << CLR_BOLD << "Installed Native Modules:" << CLR_RESET << "\n";
    std::cout << CLR_GRAY << "  Path: " << mod_path << CLR_RESET << "\n\n";

    int count = 0;
    try {
        for (const auto& entry : fs::directory_iterator(mod_path)) {
            if (!entry.is_regular_file()) continue;
            std::string name = entry.path().filename().string();
            if (name.rfind("lib") == 0) name = name.substr(3);
            auto ext = entry.path().extension().string();
            if (ext == ".dylib" || ext == ".so" || ext == ".dll") {
                name = name.substr(0, name.size() - ext.size());
                std::cout << CLR_GREEN << "  • " << CLR_RESET << name
                          << CLR_GRAY << " (" << entry.path().filename().string() << ")" << CLR_RESET << "\n";
                count++;
            }
        }
    } catch (const std::exception& e) {
        std::cout << CLR_YELLOW << "  (module directory not found: " << e.what() << ")" << CLR_RESET << "\n";
    }

    if (count == 0) {
        std::cout << CLR_YELLOW << "  No modules installed." << CLR_RESET << "\n";
    }
    std::cout << "\n";
}

static int cmd_check(const std::string& file, const CliFlags& flags) {
    if (!is_an_file(file)) {
        std::cerr << CLR_RED << "[ERROR] 'check' requires a .an source file." << CLR_RESET << "\n";
        return 1;
    }

    std::string source = angara::CompilerDriver::read_file(file);
    if (source.empty()) {
        std::cerr << CLR_RED << "[ERROR] Could not read file '" << file << "'." << CLR_RESET << "\n";
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

    std::string base_name = angara::CompilerDriver::get_base_name(file);
    angara::CompilerDriver driver;
    if (!flags.target.empty()) driver.set_target(resolve_target_triple(flags.target));
    if (!flags.sysroot.empty()) driver.set_sysroot(flags.sysroot);
    driver.set_check_only(true);
    if (flags.werror) driver.set_warnings_as_errors(true);
    for (const auto& w : flags.suppress_warnings) driver.suppress_warning(w);

    std::string native_mod_path = "/opt/angara/modules";
    if (fs::exists("build/modules")) {
        native_mod_path = fs::absolute("build/modules").string();
    }
    driver.set_paths("/opt/angara/src/modules", native_mod_path);

    angara::ProjectConfig config;
    config.name = base_name;
    config.entry_point = file;
    config.path = ".";
    config.type = angara::ProjectType::APP;

    verbose("Running typecheck for '" + file + "'...");
    if (driver.compile(config, file)) {
        std::cout << CLR_BOLD << CLR_GREEN << "[CK] " << CLR_RESET << "Typecheck passed for " << base_name << "\n";
        return 0;
    }
    return 1;
}

static int cmd_dump_ast(const std::string& file) {
    if (!is_an_file(file)) {
        std::cerr << CLR_RED << "[ERROR] --dump-ast requires a .an source file." << CLR_RESET << "\n";
        return 1;
    }

    std::string source = angara::CompilerDriver::read_file(file);
    if (source.empty()) {
        std::cerr << CLR_RED << "[ERROR] Could not read file '" << file << "'." << CLR_RESET << "\n";
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

static int cmd_compile_single_file(const std::string& source_file, const CliFlags& flags) {
    auto build_start = std::chrono::high_resolution_clock::now();
    std::string base_name = angara::CompilerDriver::get_base_name(source_file);

    angara::ProjectConfig legacy_config;
    legacy_config.name = base_name;
    legacy_config.entry_point = source_file;
    legacy_config.path = ".";
    legacy_config.type = angara::ProjectType::APP;
    legacy_config.freestanding = flags.freestanding;
    legacy_config.nostdlib = flags.nostdlib;

    if (flags.release) {
        legacy_config.profile.mode = angara::BuildMode::RELEASE;
        legacy_config.profile.opt_level = 2;
    }

    std::string resolved_target;
    if (!flags.target.empty()) {
        resolved_target = resolve_target_triple(flags.target);
    }

    angara::CompilerDriver driver;
    if (!resolved_target.empty()) driver.set_target(resolved_target);
    if (!flags.sysroot.empty()) driver.set_sysroot(flags.sysroot);
    if (flags.freestanding) driver.set_freestanding(true);
    if (flags.nostdlib) driver.set_nostdlib(true);
    if (flags.dump_ir) driver.set_dump_ir(true);
    if (flags.emit_llvm) driver.set_emit_llvm(true);
    if (flags.debug) driver.set_debug(true);
    if (flags.werror) driver.set_warnings_as_errors(true);
    for (const auto& w : flags.suppress_warnings) driver.suppress_warning(w);

    std::string native_mod_path = "/opt/angara/modules";
    if (fs::exists("build/modules")) {
        native_mod_path = fs::absolute("build/modules").string();
    }
    driver.set_paths("/opt/angara/src/modules", native_mod_path);

    if (!flags.error_format.empty()) driver.set_error_format(flags.error_format);
    driver.set_gc_strategy(flags.gc);

    verbose("Compiling '" + source_file + "'...");
    for (const auto& o : driver.get_generated_object_files()) {
        verbose("  Object: " + o);
    }

    if (!driver.compile(legacy_config, source_file)) {
        return 1;
    }

    std::cout << CLR_BOLD << CLR_CYAN << "[CX] " << CLR_RESET << "Codegen complete" << std::endl;

    if (flags.freestanding) {
        std::string obj_output = base_name + ".o";
        const auto& objs = driver.get_generated_object_files();
        if (objs.size() == 1) {
            fs::rename(*objs.begin(), obj_output);
        } else {
            obj_output = *objs.begin();
        }
        auto total_end = std::chrono::high_resolution_clock::now();
        double total_time = std::chrono::duration<double>(total_end - build_start).count();
        std::cout << CLR_BOLD << CLR_GREEN << "[OK] " << CLR_RESET << "Freestanding object emitted: " << obj_output << CLR_DIM << " (" << total_time << "s)" << CLR_RESET << "\n";
        std::cout << CLR_CYAN << "  Link with your bare-metal toolchain, e.g.:" << CLR_RESET << "\n";
        std::cout << CLR_GRAY << "  aarch64-unknown-none-elf-gcc -nostdlib -T linker.ld -o kernel "
                  << obj_output << CLR_RESET << "\n";
        return 0;
    }

    std::string binary_name = flags.output_name.empty() ? base_name : flags.output_name;

    std::cout << CLR_BOLD << CLR_CYAN << "[LK] " << CLR_RESET << "Linking " << binary_name << std::endl;

    std::stringstream cmd_link;
    cmd_link << "clang";
    if (!resolved_target.empty()) cmd_link << " -target " << angara::shell_escape(resolved_target);
    if (!flags.sysroot.empty()) cmd_link << " --sysroot " << angara::shell_escape(flags.sysroot);

    if (flags.release) cmd_link << " -O2";
    else cmd_link << " -O0";

    if (flags.debug) cmd_link << " -g";

    cmd_link << " -o " << angara::shell_escape(binary_name);

    for (const auto& o_file : driver.get_generated_object_files()) {
        cmd_link << " " << angara::shell_escape(o_file);
    }

    for (const auto& link_file : flags.link_files) {
        cmd_link << " " << angara::shell_escape(link_file);
    }

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
        cmd_link << " " << angara::shell_escape(mod_path);
    }

    if (flags.nostdlib) {
#if defined(__APPLE__)
        cmd_link << " -nodefaultlibs -lSystem -Wno-return-type";
#else
        cmd_link << " -nostdlib -Wno-return-type";
#endif
    } else {
        cmd_link << " -pthread -lm -Wno-return-type";
        cmd_link << " -Wl,-rpath,/opt/angara/modules";
    }

    verbose("Link command: " + cmd_link.str());

    int res = system(cmd_link.str().c_str());
    auto total_end = std::chrono::high_resolution_clock::now();
    double total_time = std::chrono::duration<double>(total_end - build_start).count();

    if (res == 0) {
        // Generate dSYM bundle on macOS in debug mode before cleaning up object files
        if (flags.debug) {
#ifdef __APPLE__
            std::string dsym_cmd = "dsymutil " + angara::shell_escape(binary_name) + " 2>/dev/null";
            (void)system(dsym_cmd.c_str());
#endif
        }
        for (const auto& o_file : driver.get_generated_object_files()) {
            remove(o_file.c_str());
        }
        std::cout << CLR_BOLD << CLR_GREEN << "[OK] " << CLR_RESET << "Built " << binary_name << CLR_DIM << " in " << total_time << "s" << CLR_RESET << "\n";
        return 0;
    } else {
        std::cerr << CLR_RED << "[ERROR] Linker failed for '" << binary_name << "'.\n"
                  << "         Check that all libraries are installed." << CLR_RESET << "\n";
        return 1;
    }
}

static int cmd_path_build(std::vector<std::string>& args, const CliFlags& flags) {
    if (args.size() < 2) {
        std::cerr << CLR_RED << "[ERROR] --path requires a project configuration file." << CLR_RESET << "\n";
        return 1;
    }

    std::string resolved_target;
    if (!flags.target.empty()) {
        resolved_target = resolve_target_triple(flags.target);
    }

    angara::BuildSystem builder;
    if (!resolved_target.empty()) builder.set_target(resolved_target);
    if (!flags.sysroot.empty()) builder.set_sysroot(flags.sysroot);
    if (flags.release) builder.set_build_mode(angara::BuildMode::RELEASE);
    return builder.build(args[1]) ? 0 : 1;
}

static int handle_no_args() {
    if (std::string project_file = find_local_project_file(); !project_file.empty()) {
        std::cout << CLR_BOLD << "Found project configuration: " << project_file << CLR_RESET << "\n";
        angara::BuildSystem builder;
        return builder.build(project_file) ? 0 : 1;
    }
    print_help();
    return 1;
}

static int handle_init(const std::vector<std::string>& args) {
    std::string template_name = (args.size() > 1) ? args[1] : "";
    return angara::ProjectInitializer::run(template_name) ? 0 : 1;
}

static int handle_run(const std::vector<std::string>& args) {
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

static int handle_clean(const std::vector<std::string>& args) {
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

static int handle_publish(const std::vector<std::string>& args) {
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

static int handle_check(std::vector<std::string> args) {
    args.erase(args.begin());
    CliFlags flags = CliFlags::parse(args);
    if (args.empty()) {
        std::cerr << CLR_RED << "[ERROR] 'check' requires a .an source file." << CLR_RESET << "\n";
        return 1;
    }
    return cmd_check(args[0], flags);
}

static int handle_fmt(std::vector<std::string> args) {
    args.erase(args.begin());
    bool write_in_place = false;
    bool check_mode = false;
    bool list_mode = false;
    std::vector<std::string> files;
    for (auto& arg : args) {
        if (arg == "-w" || arg == "--write") {
            write_in_place = true;
        } else if (arg == "-c" || arg == "--check") {
            check_mode = true;
        } else if (arg == "-l" || arg == "--list") {
            list_mode = true;
        } else {
            files.push_back(arg);
        }
    }

    if (files.empty()) {
        std::cerr << CLR_RED << "[ERROR] 'fmt' requires at least one .an source file." << CLR_RESET << "\n";
        std::cerr << "Usage: angc fmt [-w|--write] [-c|--check] [-l|--list] <file.an> [file2.an ...]\n";
        return 1;
    }

    int errors = 0;
    int needs_formatting = 0;
    for (auto& file : files) {
        std::ifstream ifs(file);
        if (!ifs.is_open()) {
            std::cerr << CLR_RED << "[ERROR] Cannot open file: " << file << CLR_RESET << "\n";
            errors++;
            continue;
        }
        std::string source((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        ifs.close();

        auto filename_ptr = std::make_shared<std::string>(file);
        angara::ErrorHandler errorHandler(source);
        angara::Lexer lexer(source, filename_ptr, errorHandler);
        auto tokens = lexer.scanTokens();
        if (errorHandler.hadError()) { errors++; continue; }

        angara::Parser parser(tokens, errorHandler);
        auto statements = parser.parseStmts();
        if (errorHandler.hadError()) { errors++; continue; }

        angara::Formatter formatter;
        std::string formatted = formatter.format(statements);

        if (check_mode || list_mode) {
            if (formatted != source) {
                needs_formatting++;
                if (list_mode) {
                    std::cout << file << "\n";
                } else {
                    std::cerr << CLR_RED << "[FAIL] " << CLR_RESET << file << " needs formatting\n";
                }
            } else if (check_mode && !list_mode) {
                std::cout << CLR_BOLD << CLR_GREEN << "[OK] " << CLR_RESET << file << "\n";
            }
        } else if (write_in_place) {
            std::ofstream ofs(file);
            ofs << formatted;
            ofs.close();
            std::cout << CLR_BOLD << CLR_GREEN << "[OK] " << CLR_RESET << "Formatted: " << file << "\n";
        } else {
            std::cout << formatted;
        }
    }
    if (errors > 0) return 1;
    if ((check_mode || list_mode) && needs_formatting > 0) return 1;
    return 0;
}

// ── Watch Mode ───────────────────────────────────────────────────

static int handle_explain(std::vector<std::string> args) {
    if (args.size() < 2) {
        std::cerr << CLR_RED << "[ERROR] 'explain' requires an error or warning code (e.g., W003, E377)." << CLR_RESET << "\n";
        return 1;
    }
    std::string code = args[1];

    // Warning explanations
    static const std::map<std::string, std::pair<std::string, std::string>> explanations = {
        {"W001", {"Division or modulo by zero", "The compiler detected that the divisor in a division or modulo operation is the constant zero.\n\n  Example:\n    let x = 10 / 0;    // W001\n    let y = 10 % 0;    // W001\n\n  Fix: Ensure the divisor is non-zero. If this is intentional in a\ngeneric context, suppress with -Wno-W001."}},
        {"W002", {"Using 'nil' in a logical expression", "The compiler detected 'nil' used as an operand of '&&' or '||'. Since nil is falsy, 'nil && x' always evaluates to nil and 'nil || x' always evaluates to x.\n\n  Example:\n    let result = nil && true;    // W002: always nil\n\n  Fix: Check if you meant to use an optional check instead, or\nsuppress with -Wno-W002 if this is intentional."}},
        {"W003", {"Unused variable", "A variable was declared but never read before going out of scope.\n\n  Example:\n    func foo() {\n        let x = 42;    // W003: 'x' is never used\n    }\n\n  Fix: Either use the variable, prefix it with '_' to signal\nintentional discard ('_x'), or remove the declaration entirely.\nSuppress with -Wno-W003."}},
    };

    auto it = explanations.find(code);
    if (it != explanations.end()) {
        std::cout << CLR_BOLD << CLR_CYAN << code << CLR_RESET << ": " << it->second.first << "\n\n";
        std::cout << it->second.second << "\n";
    } else if (code[0] == 'W') {
        std::cout << CLR_BOLD << CLR_YELLOW << code << CLR_RESET << " is a compiler warning.\n";
        std::cout << CLR_DIM << "  Suppress with: -Wno-" << code << "\n" << CLR_RESET;
        std::cout << "  No detailed explanation is available for this code yet.\n";
    } else if (code[0] == 'E') {
        std::cout << CLR_BOLD << CLR_RED << code << CLR_RESET << " is a compiler error.\n";
        std::cout << "  No detailed explanation is available for this code yet.\n";
        std::cout << CLR_DIM << "  The error message printed by the compiler should describe the issue.\n" << CLR_RESET;
    } else {
        std::cerr << CLR_RED << "[ERROR] Unknown code format: " << code << CLR_RESET << "\n";
        std::cerr << "  Expected a code like W003 or E377.\n";
        return 1;
    }
    return 0;
}

static int handle_test(std::vector<std::string> args);  // forward decl

static std::vector<fs::path> collect_an_files(const fs::path& root) {
    std::vector<fs::path> files;
    try {
        for (const auto& entry : fs::recursive_directory_iterator(root)) {
            if (entry.is_regular_file() && entry.path().extension() == ".an") {
                files.push_back(entry.path());
            }
        }
    } catch (...) {}
    return files;
}

struct FileSnapshot {
    fs::path path;
    fs::file_time_type last_write;
};

static std::vector<FileSnapshot> snapshot_files(const std::vector<fs::path>& files) {
    std::vector<FileSnapshot> snap;
    for (const auto& f : files) {
        try {
            snap.push_back({f, fs::last_write_time(f)});
        } catch (...) {}
    }
    return snap;
}

static bool detect_changes(const std::vector<FileSnapshot>& prev,
                           std::vector<FileSnapshot>& curr) {
    bool changed = false;
    for (size_t i = 0; i < curr.size(); i++) {
        if (i < prev.size() && curr[i].path == prev[i].path) {
            if (curr[i].last_write != prev[i].last_write) {
                changed = true;
                curr[i] = {curr[i].path, fs::last_write_time(curr[i].path)};
            }
        } else {
            changed = true;
        }
    }
    // Check for new files
    try {
        for (const auto& entry : fs::recursive_directory_iterator(".")) {
            if (entry.is_regular_file() && entry.path().extension() == ".an") {
                bool found = false;
                for (auto& c : curr) {
                    if (c.path == entry.path()) { found = true; break; }
                }
                if (!found) {
                    changed = true;
                    curr.push_back({entry.path(), fs::last_write_time(entry.path())});
                }
            }
        }
    } catch (...) {}
    return changed;
}

static int handle_watch(std::vector<std::string> args) {
    args.erase(args.begin());
    bool run_after = false;
    bool test_after = false;
    for (auto& arg : args) {
        if (arg == "-r" || arg == "--run") run_after = true;
        else if (arg == "-t" || arg == "--test") test_after = true;
    }

    std::string project_file = find_local_project_file();
    if (project_file.empty()) {
        std::cerr << CLR_RED << "[ERROR] No .abs project file found in the current directory." << CLR_RESET << "\n";
        return 1;
    }

    std::cout << CLR_BOLD << CLR_CYAN << "[WATCH] " << CLR_RESET
              << "Watching for changes (project: " << project_file << ")\n";
    if (run_after) std::cout << CLR_DIM << "  --run enabled: will execute after each build\n" << CLR_RESET;
    if (test_after) std::cout << CLR_DIM << "  --test enabled: will run tests after each build\n" << CLR_RESET;
    std::cout << CLR_DIM << "  Press Ctrl+C to stop\n" << CLR_RESET << "\n";

    auto an_files = collect_an_files(".");
    auto snapshot = snapshot_files(an_files);

    // Initial build
    {
        auto t0 = std::chrono::high_resolution_clock::now();
        std::cout << CLR_BOLD << CLR_CYAN << "[WATCH] " << CLR_RESET << "Initial build...\n";
        angara::BuildSystem builder;
        bool ok = builder.build(project_file);
        auto t1 = std::chrono::high_resolution_clock::now();
        double secs = std::chrono::duration<double>(t1 - t0).count();

        if (ok) {
            std::cout << CLR_BOLD << CLR_GREEN << "[WATCH] " << CLR_RESET
                      << "Build OK" << CLR_DIM << " (" << secs << "s)" << CLR_RESET << "\n";
            if (run_after) builder.run(project_file);
            if (test_after) {
                std::vector<std::string> test_args = {"test"};
                handle_test(test_args);
            }
        } else {
            std::cout << CLR_BOLD << CLR_RED << "[WATCH] " << CLR_RESET << "Build failed\n";
        }
        std::cout << CLR_DIM << "  Waiting for changes...\n" << CLR_RESET;
    }

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        auto current_files = collect_an_files(".");
        auto current_snap = snapshot_files(current_files);
        if (!detect_changes(snapshot, current_snap)) continue;
        snapshot = current_snap;

        // Debounce: wait for changes to settle
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        current_snap = snapshot_files(collect_an_files("."));
        snapshot = current_snap;

        auto t0 = std::chrono::high_resolution_clock::now();
        std::cout << "\n" << CLR_BOLD << CLR_CYAN << "[WATCH] " << CLR_RESET
                  << "Change detected, rebuilding...\n";

        angara::BuildSystem builder;
        bool ok = builder.build(project_file);
        auto t1 = std::chrono::high_resolution_clock::now();
        double secs = std::chrono::duration<double>(t1 - t0).count();

        if (ok) {
            std::cout << CLR_BOLD << CLR_GREEN << "[WATCH] " << CLR_RESET
                      << "Build OK" << CLR_DIM << " (" << secs << "s)" << CLR_RESET << "\n";
            if (run_after) builder.run(project_file);
            if (test_after) {
                std::vector<std::string> test_args = {"test"};
                handle_test(test_args);
            }
        } else {
            std::cout << CLR_BOLD << CLR_RED << "[WATCH] " << CLR_RESET << "Build failed\n";
        }
        std::cout << CLR_DIM << "  Waiting for changes...\n" << CLR_RESET;
    }
    return 0;
}

static int handle_test(std::vector<std::string> args) {
    args.erase(args.begin());
    CliFlags flags = CliFlags::parse(args);

    // Find test directory
    std::string test_dir = "tests";
    if (!args.empty() && fs::is_directory(args[0])) {
        test_dir = args[0];
    }

    if (!fs::is_directory(test_dir)) {
        std::cerr << CLR_RED << "[ERROR] Test directory '" << test_dir << "' not found." << CLR_RESET << "\n";
        return 1;
    }

    std::string angc_bin = "build/angc";

    int pass = 0, fail = 0;
    std::vector<std::string> bugs;

    std::cout << "\n" << CLR_BOLD << CLR_CYAN << "Angara Test Runner" << CLR_RESET << "\n\n";

    // Run positive tests (should compile & run successfully)
    std::string pos_dir = test_dir + "/positive";
    if (fs::is_directory(pos_dir)) {
        std::cout << CLR_BOLD << CLR_CYAN << "-- Positive Tests (should compile & run) --" << CLR_RESET << "\n\n";
        for (const auto& entry : fs::directory_iterator(pos_dir)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".an") continue;
            std::string test_name = entry.path().stem().string();
            std::string test_file = entry.path().string();

            std::cout << "  " << CLR_BOLD << test_name << CLR_RESET << ": ";

            // Compile
            std::string binary = "/tmp/angara_test_" + test_name;
            std::string compile_cmd = angc_bin + " \"" + test_file + "\" -o \"" + binary + "\" 2>&1";
            FILE* pipe = popen(compile_cmd.c_str(), "r");
            if (!pipe) {
                std::cout << CLR_RED << "ERROR" << CLR_RESET << " (popen failed)\n";
                fail++;
                continue;
            }
            char buffer[4096];
            std::string compile_output;
            while (fgets(buffer, sizeof(buffer), pipe)) compile_output += buffer;
            int compile_rc = pclose(pipe);

            if (compile_rc != 0) {
                if (compile_output.find("SIGABRT") != std::string::npos ||
                    compile_output.find("SIGSEGV") != std::string::npos) {
                    std::cout << CLR_RED << "CRASH" << CLR_RESET << " (compiler crashed)\n";
                    bugs.push_back("BUG [" + test_name + "]: Compiler crash");
                } else if (compile_output.find("Linker") != std::string::npos ||
                           compile_output.find("Undefined") != std::string::npos) {
                    std::cout << CLR_RED << "LINKER ERROR" << CLR_RESET << "\n";
                    bugs.push_back("BUG [" + test_name + "]: Linker error");
                } else {
                    std::cout << CLR_RED << "COMPILE ERROR" << CLR_RESET << "\n";
                    bugs.push_back("BUG [" + test_name + "]: Unexpected compile error");
                }
                fail++;
                continue;
            }

            // Run
            if (!fs::exists(binary)) {
                std::cout << CLR_RED << "NO BINARY" << CLR_RESET << "\n";
                fail++;
                continue;
            }

            std::string run_cmd = "\"" + binary + "\" 2>&1";
            pipe = popen(run_cmd.c_str(), "r");
            if (!pipe) {
                std::cout << CLR_RED << "ERROR" << CLR_RESET << " (popen failed)\n";
                fail++;
                continue;
            }
            std::string run_output;
            while (fgets(buffer, sizeof(buffer), pipe)) run_output += buffer;
            int run_rc = pclose(pipe);
            fs::remove(binary);

            if (run_rc != 0) {
                if (run_rc == 139) {
                    std::cout << CLR_RED << "SEGFAULT" << CLR_RESET << " (runtime)\n";
                    bugs.push_back("BUG [" + test_name + "]: Runtime segfault");
                } else if (run_rc == 134) {
                    std::cout << CLR_RED << "ABORT" << CLR_RESET << " (runtime)\n";
                    bugs.push_back("BUG [" + test_name + "]: Runtime abort");
                } else {
                    std::cout << CLR_RED << "RUNTIME ERROR" << CLR_RESET << " (exit " << (run_rc >> 8) << ")\n";
                    bugs.push_back("BUG [" + test_name + "]: Runtime error");
                }
                fail++;
                continue;
            }

            std::cout << CLR_BOLD << CLR_GREEN << "PASS" << CLR_RESET << "\n";
            pass++;
        }
    }

    // Run negative tests (should produce compilation errors)
    std::string neg_dir = test_dir + "/negative";
    if (fs::is_directory(neg_dir)) {
        std::cout << "\n" << CLR_BOLD << CLR_CYAN << "-- Negative Tests (should fail to compile) --" << CLR_RESET << "\n\n";
        for (const auto& entry : fs::directory_iterator(neg_dir)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".an") continue;
            std::string test_name = entry.path().stem().string();
            std::string test_file = entry.path().string();

            std::cout << "  " << CLR_BOLD << test_name << CLR_RESET << ": ";

            std::string binary = "/tmp/angara_test_neg_" + test_name;
            std::string compile_cmd = angc_bin + " \"" + test_file + "\" -o \"" + binary + "\" 2>&1";
            FILE* pipe = popen(compile_cmd.c_str(), "r");
            if (!pipe) {
                std::cout << CLR_RED << "ERROR" << CLR_RESET << " (popen failed)\n";
                fail++;
                continue;
            }
            char buffer[4096];
            std::string compile_output;
            while (fgets(buffer, sizeof(buffer), pipe)) compile_output += buffer;
            int compile_rc = pclose(pipe);

            if (compile_rc == 0) {
                std::cout << CLR_RED << "MISSING ERROR" << CLR_RESET << " (compiled but should have failed)\n";
                bugs.push_back("BUG [" + test_name + "]: Failed to catch error");
                fail++;
                continue;
            }

            if (compile_output.find("SIGABRT") != std::string::npos ||
                compile_output.find("SIGSEGV") != std::string::npos) {
                std::cout << CLR_RED << "CRASH" << CLR_RESET << " (crashed instead of error)\n";
                bugs.push_back("BUG [" + test_name + "]: Crash instead of error");
                fail++;
            } else {
                // Extract the first error line
                std::string error_line;
                std::istringstream iss(compile_output);
                std::string line;
                while (std::getline(iss, line)) {
                    if (line.find("Error") != std::string::npos || line.find("error") != std::string::npos) {
                        // Strip ANSI
                        std::string clean;
                        for (char c : line) {
                            if (c == '\033') { while (line.find('m', line.find(c)) != std::string::npos) break; continue; }
                            clean += c;
                        }
                        error_line = clean;
                        break;
                    }
                }
                if (!error_line.empty()) {
                    std::cout << CLR_BOLD << CLR_GREEN << "CAUGHT" << CLR_RESET << " " << error_line << "\n";
                } else {
                    std::cout << CLR_BOLD << CLR_GREEN << "CAUGHT" << CLR_RESET << "\n";
                }
                pass++;
            }
        }
    }

    // Print summary
    int total = pass + fail;
    std::cout << "\n" << CLR_BOLD << "Results: " << CLR_GREEN << pass << " passed" << CLR_RESET;
    if (fail > 0) {
        std::cout << ", " << CLR_RED << fail << " failed" << CLR_RESET;
    }
    std::cout << " out of " << total << " tests\n";

    if (!bugs.empty()) {
        std::cout << "\n" << CLR_BOLD << CLR_YELLOW << "Bug Report:" << CLR_RESET << "\n";
        for (size_t i = 0; i < bugs.size(); i++) {
            std::cout << "  " << CLR_YELLOW << (i + 1) << ". " << bugs[i] << CLR_RESET << "\n";
        }
    }

    std::cout << "\n";
    return fail;
}

int main(int argc, char* argv[]) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);

    if (args.empty()) {
        return handle_no_args();
    }

    const std::string cmd = args[0];

    if (cmd == "init")    return handle_init(args);
    if (cmd == "run")     return handle_run(args);
    if (cmd == "test")    return handle_test(args);
    if (cmd == "clean")   return handle_clean(args);
    if (cmd == "publish") return handle_publish(args);
    if (cmd == "modules") { list_modules(); return 0; }
    if (cmd == "check")   return handle_check(args);
    if (cmd == "fmt")     return handle_fmt(args);
    if (cmd == "watch")   return handle_watch(args);
    if (cmd == "explain") return handle_explain(args);
    if (cmd == "lsp")     { angara::LSPServer lsp; return lsp.run(); }
    if (cmd == "repl")    { angara::REPL repl; return repl.run(); }
    if (cmd == "-v" || cmd == "--version") { print_version(); return 0; }
    if (cmd == "-h" || cmd == "--help")    { print_help(); return 0; }
    if (cmd == "--make-perfect") { angara::run_easter_egg(); return 0; }

    CliFlags flags = CliFlags::parse(args);

    if (flags.dump_ast && !args.empty()) return cmd_dump_ast(args[0]);
    if (cmd == "--path") return cmd_path_build(args, flags);
    if (!args.empty() && is_an_file(args[0])) return cmd_compile_single_file(args[0], flags);

    std::cerr << CLR_RED << "[ERROR] Unknown command: " << cmd << CLR_RESET << "\n";
    print_help();
    return 1;
}
