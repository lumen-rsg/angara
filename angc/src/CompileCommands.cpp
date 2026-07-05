#include "StringUtils.h"
#include "CLI.h"
#include "CompilerDriver.h"
#include "Lexer.h"
#include "Parser.h"
#include "ErrorHandler.h"
#include "Colors.h"
#include "Platform.h"
#include "../analyzer/printer/ASTPrinter.h"
#include "BuildSystem.h"
#include <iostream>
#include <filesystem>
#include <sstream>
namespace fs = std::filesystem;
namespace angara {

int angara::CLI::cmdCheck(const std::string& file) {
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
    if (!m_flags.target.empty()) driver.set_target(resolve_target_triple(m_flags.target));
    if (!m_flags.sysroot.empty()) driver.set_sysroot(m_flags.sysroot);
    driver.set_check_only(true);
    if (m_flags.werror) driver.set_warnings_as_errors(true);
    for (const auto& w : m_flags.suppress_warnings) driver.suppress_warning(w);

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

    this->verbose("Running typecheck for '" + file + "'...");
    if (driver.compile(config, file)) {
        std::cout << CLR_BOLD << CLR_GREEN << "[CK] " << CLR_RESET << "Typecheck passed for " << base_name << "\n";
        return 0;
    }
    return 1;
}

int angara::CLI::cmdDumpAst(const std::string& file) {
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

int angara::CLI::cmdCompileSingleFile(const std::string& source_file) {
    auto build_start = std::chrono::high_resolution_clock::now();
    std::string base_name = angara::CompilerDriver::get_base_name(source_file);

    angara::ProjectConfig legacy_config;
    legacy_config.name = base_name;
    legacy_config.entry_point = source_file;
    legacy_config.path = ".";
    legacy_config.type = angara::ProjectType::APP;
    legacy_config.freestanding = m_flags.freestanding;
    legacy_config.nostdlib = m_flags.nostdlib;

    if (m_flags.release) {
        legacy_config.profile.mode = angara::BuildMode::RELEASE;
        legacy_config.profile.opt_level = 2;
    }

    std::string resolved_target;
    if (!m_flags.target.empty()) {
        resolved_target = resolve_target_triple(m_flags.target);
    }

    angara::CompilerDriver driver;
    if (!resolved_target.empty()) driver.set_target(resolved_target);
    if (!m_flags.sysroot.empty()) driver.set_sysroot(m_flags.sysroot);
    if (m_flags.freestanding) driver.set_freestanding(true);
    if (m_flags.nostdlib) driver.set_nostdlib(true);
    if (m_flags.dump_ir) driver.set_dump_ir(true);
    if (m_flags.emit_llvm) driver.set_emit_llvm(true);
    if (m_flags.debug) driver.set_debug(true);
    if (m_flags.werror) driver.set_warnings_as_errors(true);
    for (const auto& w : m_flags.suppress_warnings) driver.suppress_warning(w);

    // TOOL-2: parallel + incremental flags.
    if (m_flags.jobs > 0) driver.set_jobs(m_flags.jobs);
    if (m_flags.force_rebuild) driver.set_force_rebuild(true);
    // Use .angara/build as the build dir for single-file compilations too,
    // so incremental caching works.
    driver.set_build_dir(".angara/build/obj");

    std::string native_mod_path = "/opt/angara/modules";
    if (fs::exists("build/modules")) {
        native_mod_path = fs::absolute("build/modules").string();
    }
    driver.set_paths("/opt/angara/src/modules", native_mod_path);

    if (!m_flags.error_format.empty()) driver.set_error_format(m_flags.error_format);

    this->verbose("Compiling '" + source_file + "'...");
    for (const auto& o : driver.get_generated_object_files()) {
        this->verbose("  Object: " + o);
    }

    if (!driver.compile(legacy_config, source_file)) {
        return 1;
    }

    std::cout << CLR_BOLD << CLR_CYAN << "[CX] " << CLR_RESET << "Codegen complete" << std::endl;

    if (m_flags.freestanding) {
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

    std::string binary_name = m_flags.output_name.empty() ? base_name : m_flags.output_name;

    std::cout << CLR_BOLD << CLR_CYAN << "[LK] " << CLR_RESET << "Linking " << binary_name << std::endl;

    std::stringstream cmd_link;
    cmd_link << "clang";
    if (!resolved_target.empty()) cmd_link << " -target " << angara::shell_escape(resolved_target);
    if (!m_flags.sysroot.empty()) cmd_link << " --sysroot " << angara::shell_escape(m_flags.sysroot);

    if (m_flags.release) cmd_link << " -O2";
    else cmd_link << " -O0";

    if (m_flags.debug) cmd_link << " -g";

    cmd_link << " -o " << angara::shell_escape(binary_name);

    for (const auto& o_file : driver.get_generated_object_files()) {
        cmd_link << " " << angara::shell_escape(o_file);
    }

    for (const auto& link_file : m_flags.link_files) {
        cmd_link << " " << angara::shell_escape(link_file);
    }

    std::set<std::string> libs;
    for (const auto& lib : driver.get_native_libs_linked()) {
        libs.insert(lib);
    }
    for (const auto& lib : libs) {
        std::string mod_path;
        std::string local_mod = (fs::path("build/modules") / (lib + ANGARA_NATIVE_EXT)).string();
        std::string installed_mod = "/opt/angara/modules/" + lib + ANGARA_NATIVE_EXT;
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

    if (m_flags.nostdlib) {
#if defined(__APPLE__)
        cmd_link << " -nodefaultlibs -lSystem -Wno-return-type";
#else
        cmd_link << " -nostdlib -Wno-return-type";
#endif
    } else {
        // RT-6: position-independent executable (matches LLVMBackend's PIC
        // relocation model). Freestanding/nostdlib builds stay non-PIE.
        cmd_link << " -fPIE -pie";
        cmd_link << " -pthread -lm -Wno-return-type";
        cmd_link << " -Wl,-rpath,/opt/angara/modules";
    }

    this->verbose("Link command: " + cmd_link.str());

    int res = system(cmd_link.str().c_str());
    auto total_end = std::chrono::high_resolution_clock::now();
    double total_time = std::chrono::duration<double>(total_end - build_start).count();

    if (res == 0) {
        // Generate dSYM bundle on macOS in debug mode
        if (m_flags.debug) {
#ifdef __APPLE__
            std::string dsym_cmd = "dsymutil " + angara::shell_escape(binary_name) + " 2>/dev/null";
            (void)system(dsym_cmd.c_str());
#endif
        }
        // TOOL-2: keep .o files for incremental compilation.
        // They live in .angara/build/obj/ and are cleaned by `angc clean`.
        std::cout << CLR_BOLD << CLR_GREEN << "[OK] " << CLR_RESET << "Built " << binary_name << CLR_DIM << " in " << total_time << "s" << CLR_RESET << "\n";
        return 0;
    } else {
        std::cerr << CLR_RED << "[ERROR] Linker failed for '" << binary_name << "'.\n"
                  << "         Check that all libraries are installed." << CLR_RESET << "\n";
        return 1;
    }
}

int angara::CLI::cmdPathBuild(std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << CLR_RED << "[ERROR] --path requires a project configuration file." << CLR_RESET << "\n";
        return 1;
    }

    std::string resolved_target;
    if (!m_flags.target.empty()) {
        resolved_target = resolve_target_triple(m_flags.target);
    }

    angara::BuildSystem builder;
    if (!resolved_target.empty()) builder.set_target(resolved_target);
    if (!m_flags.sysroot.empty()) builder.set_sysroot(m_flags.sysroot);
    if (m_flags.release) builder.set_build_mode(angara::BuildMode::RELEASE);
    if (m_flags.debug) builder.set_build_mode(angara::BuildMode::DEBUG);
    // TOOL-2: parallel + incremental flags.
    if (m_flags.jobs > 0) builder.set_jobs(m_flags.jobs);
    if (m_flags.force_rebuild) builder.set_force_rebuild(true);
    return builder.build(args[1]) ? 0 : 1;
}

int angara::CLI::handleCheck(std::vector<std::string> args) {
    CliFlags flags = m_flags = CliFlags{}; parseFlags(args);
    if (args.empty()) {
        std::cerr << CLR_RED << "[ERROR] 'check' requires a .an source file." << CLR_RESET << "\n";
        return 1;
    }
    return cmdCheck(args[0]);
}
} // namespace angara
