#include "CLI.h"
#include "Colors.h"
#include "EasterEgg.h"
#include "LSPServer.h"
#include "REPL.h"
#include <iostream>
#include <vector>
#include <string>

namespace angara {

void CLI::verbose(const std::string& msg) {
    if (!m_verbose) return;
    std::cout << CLR_DIM << "  " << msg << CLR_RESET << "\n";
}

void CLI::parseFlags(std::vector<std::string>& args) {
    for (size_t i = 0; i < args.size(); ) {
        if (args[i] == "--target" && i + 1 < args.size()) {
            m_flags.target = args[i + 1];
            args.erase(args.begin() + i, args.begin() + i + 2);
        } else if (args[i] == "--sysroot" && i + 1 < args.size()) {
            m_flags.sysroot = args[i + 1];
            args.erase(args.begin() + i, args.begin() + i + 2);
        } else if ((args[i] == "-o" || args[i] == "--output") && i + 1 < args.size()) {
            m_flags.output_name = args[i + 1];
            args.erase(args.begin() + i, args.begin() + i + 2);
        } else if ((args[i] == "-l" || args[i] == "--link") && i + 1 < args.size()) {
            m_flags.link_files.push_back(args[i + 1]);
            args.erase(args.begin() + i, args.begin() + i + 2);
        } else if (args[i] == "--dump-ast") {
            m_flags.dump_ast = true;
            args.erase(args.begin() + i);
        } else if (args[i] == "--dump-ir") {
            m_flags.dump_ir = true;
            args.erase(args.begin() + i);
        } else if (args[i] == "--emit-llvm") {
            m_flags.emit_llvm = true;
            args.erase(args.begin() + i);
        } else if (args[i] == "--freestanding") {
            m_flags.freestanding = true;
            args.erase(args.begin() + i);
        } else if (args[i] == "--nostdlib") {
            m_flags.nostdlib = true;
            args.erase(args.begin() + i);
        } else if (args[i] == "--release") {
            m_flags.release = true;
            args.erase(args.begin() + i);
        } else if (args[i] == "-V" || args[i] == "--verbose") {
            m_flags.verbose_flag = true;
            m_verbose = true;
            args.erase(args.begin() + i);
        } else if (args[i] == "--debug") {
            m_flags.debug = true;
            args.erase(args.begin() + i);
        } else if (args[i] == "-Wall") {
            m_flags.wall = true;
            args.erase(args.begin() + i);
        } else if (args[i] == "-Werror") {
            m_flags.werror = true;
            args.erase(args.begin() + i);
        } else if (args[i].substr(0, 5) == "-Wno-") {
            m_flags.suppress_warnings.push_back(args[i].substr(5));
            args.erase(args.begin() + i);
        } else if (args[i] == "--error-format" && i + 1 < args.size()) {
            m_flags.error_format = args[i + 1];
            args.erase(args.begin() + i, args.begin() + i + 2);
        } else {
            ++i;
        }
    }
}

int CLI::handleLsp() {
    LSPServer lsp;
    return lsp.run();
}

int CLI::handleRepl() {
    REPL repl;
    return repl.run();
}

int CLI::run(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);

    if (args.empty()) {
        return handleNoArgs();
    }

    std::string cmd = args[0];
    args.erase(args.begin());

    if (cmd == "init")     return handleInit(args);
    if (cmd == "run")      return handleRun(args);
    if (cmd == "test")     return handleTest(args);
    if (cmd == "clean")    return handleClean(args);
    if (cmd == "publish")  return handlePublish(args);
    if (cmd == "modules")  { list_modules(); return 0; }
    if (cmd == "check")    return handleCheck(args);
    if (cmd == "fmt")      return handleFmt(args);
    if (cmd == "watch")    return handleWatch(args);
    if (cmd == "explain")  return handleExplain(args);
    if (cmd == "lsp")      return handleLsp();
    if (cmd == "repl")     return handleRepl();
    if (cmd == "-v" || cmd == "--version") { print_version(); return 0; }
    if (cmd == "-h" || cmd == "--help")    { print_help(); return 0; }
    if (cmd == "--make-perfect")           { run_easter_egg(); return 0; }

    // Not a recognized command — treat as file/flags.
    // Re-insert cmd so flag parsing sees all args (the first arg may be
    // the filename, not a flag).
    args.insert(args.begin(), cmd);
    parseFlags(args);

    if (m_flags.dump_ast && !args.empty()) {
        return cmdDumpAst(args[0]);
    }

    if (!m_flags.target.empty()) {
        // Target specified via --target; resolve and rebuild
    }

    // --path <file.abs> build
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i] == "--path" && i + 1 < args.size()) {
            return cmdPathBuild(args);
        }
    }

    // Single .an file compilation
    if (!args.empty() && is_an_file(args[0])) {
        return cmdCompileSingleFile(args[0]);
    }

    // Unknown
    std::cerr << CLR_RED << "Unknown command or file: " << cmd << CLR_RESET << "\n";
    print_help();
    return 1;
}

} // namespace angara
