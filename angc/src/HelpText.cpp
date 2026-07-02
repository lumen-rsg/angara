#include "CLI.h"
#include "Colors.h"
#include <iostream>
#include <llvm/Config/llvm-config.h>
namespace angara {
void print_help() {
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
    std::cout << std::endl;
}

void print_version() {
    std::cout << CLR_GREEN << CLR_BOLD << "angc" << CLR_RESET << ": Angara Compiler\n";
    std::cout << CLR_CYAN << "  • Compiler: " << CLR_RESET << ANGC_VERSION << "\n";
    std::cout << CLR_CYAN << "  • Backend:  " << CLR_RESET << BACKEND_VERSION << " (LLVM " << LLVM_VERSION_MAJOR << "." << LLVM_VERSION_MINOR << "." << LLVM_VERSION_PATCH << ")\n";
    std::cout << CLR_CYAN << "  • Spec:     " << CLR_RESET << ANGARA_SPEC << "\n";
    std::cout << CLR_GRAY << "  (c) 2026 Lumina Labs. This is a testing build." << CLR_RESET << "\n";
}
} // namespace angara
