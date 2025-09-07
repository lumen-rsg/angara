#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <memory>

#include "ErrorHandler.h"
#include "Lexer.h"
#include "Parser.h"
#include "TypeChecker.h"
#include "CTranspiler.h"

// --- ANSI Color Codes for beautiful output ---
const char* const RESET = "\033[0m";
const char* const BOLD = "\033[1m";
const char* const GREEN = "\033[32m";
const char* const YELLOW = "\033[33m";
const char* const RED = "\033[31m";
const char* const BLUE = "\033[34m";

// --- Helper for logging build steps ---
void log_step(const std::string& message) {
    std::cout << BOLD << GREEN << "-> " << RESET << BOLD << message << RESET << std::endl;
}

// --- Helper to get the base name of a file (e.g., "path/to/test.an" -> "test") ---
std::string get_base_name(const std::string& path) {
    size_t last_slash = path.find_last_of("/\\");
    last_slash = (last_slash == std::string::npos) ? 0 : last_slash + 1;
    size_t last_dot = path.find_last_of('.');
    if (last_dot == std::string::npos || last_dot < last_slash) {
        last_dot = path.length();
    }
    return path.substr(last_slash, last_dot - last_slash);
}

// --- The main compilation pipeline function ---
bool compile_file(const std::string& path) {
    log_step("Starting compilation for: " + path);

    // --- 1. Reading Source ---
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << RED << "Error: Could not open source file '" << path << "'" << RESET << std::endl;
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();

    angara::ErrorHandler errorHandler(source);

    // --- 2. Frontend: Lexing & Parsing ---
    log_step("Lexing & Parsing...");
    angara::Lexer lexer(source);
    auto tokens = lexer.scanTokens();
    angara::Parser parser(tokens, errorHandler);
    auto statements = parser.parseStmts();
    if (errorHandler.hadError()) {
        std::cerr << RED << "Compilation failed: Parsing errors." << RESET << std::endl;
        return false;
    }

    // --- 3. Semantic Analysis: Type Checking ---
    log_step("Performing static analysis and type checking...");
    angara::TypeChecker typeChecker(errorHandler);
    if (!typeChecker.check(statements)) {
        std::cerr << RED << "Compilation failed: Type errors." << RESET << std::endl;
        return false;
    }
    std::cout << GREEN << "   ... All type checks passed!" << RESET << std::endl;

    // --- 4. Backend: Transpiling to C ---
    log_step("Generating C code...");
    std::string base_name = get_base_name(path);
    angara::CTranspiler transpiler(typeChecker, errorHandler);
    auto [header_code, source_code] = transpiler.generate(statements, base_name);
    if (header_code.empty() && source_code.empty()) {
        std::cerr << RED << "Compilation failed: C code generation errors." << RESET << std::endl;
        return false;
    }

    // --- 5. Writing C source files ---
    std::string h_filename = base_name + ".h";
    std::string c_filename = base_name + ".c";
    log_step("Writing intermediate files (" + h_filename + ", " + c_filename + ")...");
    std::ofstream h_file(h_filename);
    h_file << header_code;
    h_file.close();
    std::ofstream c_file(c_filename);
    c_file << source_code;
    c_file.close();

    // --- 6. Invoking C Compiler (GCC/Clang) ---
    log_step("Compiling C code with system compiler (gcc)...");
    // NOTE: This assumes `angara_runtime.c` is in a place the compiler can find it.
    // A real build system would handle this better. Let's assume it's in `src/runtime`.
    std::string command = "gcc -o " + base_name + " " + c_filename + " src/runtime/angara_runtime.c -I. -Isrc/runtime -pthread -lm";
    std::cout << YELLOW << "   $ " << command << RESET << std::endl;

    int result = system(command.c_str());

    if (result != 0) {
        std::cerr << RED << "Compilation failed: C compiler returned a non-zero exit code." << RESET << std::endl;
        return false;
    }

    log_step("Success! Executable created: ./" + base_name);
    return true;
}


int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: angara_compiler <source_file.an>" << std::endl;
        return 1;
    }
    compile_file(argv[1]);
    return 0;
}