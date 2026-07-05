#include "REPL.h"
#include "CLI.h"
#include "Lexer.h"
#include "Parser.h"
#include "TypeChecker.h"
#include "CompilerDriver.h"
#include "ErrorHandler.h"
#include "LLVMBackend.h"

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/TargetSelect.h>

#include <iostream>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include <cstring>

namespace angara {

static bool isDeclaration(const std::string& input) {
    std::string trimmed = input;
    while (!trimmed.empty() && (trimmed[0] == ' ' || trimmed[0] == '\t'))
        trimmed.erase(trimmed.begin());
    return trimmed.starts_with("func ") || trimmed.starts_with("class ") ||
           trimmed.starts_with("data ") || trimmed.starts_with("enum ") ||
           trimmed.starts_with("trait ") || trimmed.starts_with("contract ") ||
           trimmed.starts_with("attach ") || trimmed.starts_with("export ") ||
           trimmed.starts_with("foreign ");
}

static bool isExpression(const std::string& input) {
    // Heuristic: if it doesn't end with ';' or '}' and isn't a keyword-start,
    // treat it as a standalone expression to be auto-printed.
    std::string trimmed = input;
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t'))
        trimmed.pop_back();
    if (trimmed.empty()) return false;
    if (trimmed.back() == ';' || trimmed.back() == '{' || trimmed.back() == '}') return false;
    // Keywords that start statements
    if (trimmed.starts_with("let ") || trimmed.starts_with("const ") ||
        trimmed.starts_with("return ") || trimmed.starts_with("if ") ||
        trimmed.starts_with("while ") || trimmed.starts_with("for ") ||
        trimmed.starts_with("try ") || trimmed.starts_with("throw "))
        return false;
    return true;
}

void REPL::printPrompt() {
    std::cout << "\033[1;36mangara> \033[0m" << std::flush;
}

std::string REPL::readLine() {
    std::string line;
    if (!std::getline(std::cin, line)) return "";

    // Track brace depth for multi-line input
    int depth = 0;
    for (char c : line) {
        if (c == '{') depth++;
        else if (c == '}') depth--;
    }

    while (depth > 0) {
        std::cout << "\033[1;36m  ...> \033[0m" << std::flush;
        std::string continuation;
        if (!std::getline(std::cin, continuation)) break;
        line += "\n" + continuation;
        for (char c : continuation) {
            if (c == '{') depth++;
            else if (c == '}') depth--;
        }
    }

    return line;
}

void REPL::processInput(const std::string& input) {
    m_line_count++;
    std::string wrapped_source;

    if (isDeclaration(input)) {
        // Accumulate declarations
        m_preamble += input + "\n";
        wrapped_source = m_preamble + "func main() {}\n";
    } else if (isExpression(input)) {
        // Standalone expression - auto-print the result
        wrapped_source = m_preamble + "func main() {\n    println(" + input + ");\n}\n";
    } else {
        // Statement - wrap in main function
        wrapped_source = m_preamble + "func main() {\n    " + input + "\n}\n";
    }

    // Lex + Parse + TypeCheck + Codegen using existing pipeline
    auto filename_ptr = std::make_shared<std::string>("<repl>");
    ErrorHandler errorHandler(wrapped_source);

    Lexer lexer(wrapped_source, filename_ptr, errorHandler);
    auto tokens = lexer.scanTokens();
    if (errorHandler.hadError()) return;

    Parser parser(tokens, errorHandler);
    auto statements = parser.parseStmts();
    if (errorHandler.hadError()) return;

    // Type check
    CompilerDriver driver;
    driver.set_check_only(false);

    std::string native_mod_path = "/opt/angara/modules";
    if (std::filesystem::exists("build/modules")) {
        native_mod_path = std::filesystem::absolute("build/modules").string();
    }
    driver.set_paths("/opt/angara/src/modules", native_mod_path);

    std::string base_name = "repl_" + std::to_string(m_line_count);
    TypeChecker typeChecker(driver, errorHandler, base_name);
    typeChecker.check(statements);
    if (errorHandler.hadError()) return;

    // Generate LLVM IR
    LLVMBackend backend(typeChecker, errorHandler);
    std::vector<std::string> all_mods;
    auto modulePair = backend.generateIR(statements, nullptr, all_mods);
    if (!modulePair.first) return;

    // Use LLJIT for in-memory execution
    auto jit = llvm::orc::LLJITBuilder().create();
    if (!jit) {
        std::cerr << "JIT error: " << llvm::toString(jit.takeError()) << "\n";
        return;
    }

    auto& j = *jit;
    auto tsm = llvm::orc::ThreadSafeModule(
        std::move(modulePair.first), std::move(modulePair.second));
    auto err = j->addIRModule(std::move(tsm));
    if (err) {
        std::cerr << "JIT add module error: " << llvm::toString(std::move(err)) << "\n";
        return;
    }

    // Look up and call main
    auto mainSym = j->lookup("__ang_main_main");
    if (!mainSym) {
        // Try without module prefix
        mainSym = j->lookup("main");
    }
    if (!mainSym) {
        auto err = mainSym.takeError();
        std::string errStr;
        llvm::raw_string_ostream ss(errStr);
        ss << err;
        // Only print if it's not a "symbol not found" for the non-declaration case
        llvm::consumeError(std::move(err));
        if (isDeclaration(input)) {
            // Declarations don't produce output - that's fine
        } else {
            std::cerr << "Error: could not execute REPL input\n";
        }
        return;
    }

    // main() returns i32
    auto* mainFn = mainSym->toPtr<int32_t(*)()>();
    mainFn();
}

int REPL::run() {
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();

    std::cout << "\033[1;35mAngara REPL v" << ANGC_VERSION << "\033[0m\n";
    std::cout << "Type \033[1m:quit\033[0m or \033[1m:q\033[0m to exit.\n\n";

    while (true) {
        printPrompt();
        std::string input = readLine();
        if (std::cin.eof()) break;
        if (input.empty()) continue;
        if (input == ":quit" || input == ":q") break;
        if (input == ":help" || input == ":h") {
            std::cout << "  :quit / :q   Exit REPL\n";
            std::cout << "  :help / :h   Show this help\n";
            continue;
        }

        processInput(input);
    }

    std::cout << "\nBye!\n";
    return 0;
}

} // namespace angara
