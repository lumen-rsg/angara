//
// Created by cv2 on 07.09.2025.
//

#include "CompilerDriver.h"
#include "ErrorHandler.h"
#include "Lexer.h"
#include "Parser.h"
#include "TypeChecker.h"
#include "CTranspiler.h"
#include <iostream>
#include <fstream>
#include <sstream>

const char* const RESET = "\033[0m";
const char* const BOLD = "\033[1m";
const char* const RED = "\033[31m";
const char* const GREEN = "\033[32m";
const char* const YELLOW = "\033[33m";

namespace angara {


void CompilerDriver::log_step(const std::string& message) {
    std::cout << BOLD << GREEN << "-> " << RESET << BOLD << message << RESET << std::endl;
}

CompilerDriver::CompilerDriver() {}

void CompilerDriver::print_progress(const std::string& current_file) {
    // This creates a progress bar like: [===>      ] (3/10) Compiling: utils.an
    int bar_width = 20;
    float progress = (m_total_modules > 0) ? (float)m_modules_compiled / m_total_modules : 0;
    int pos = bar_width * progress;

    std::cout << "[";
    for (int i = 0; i < bar_width; ++i) {
        if (i < pos) std::cout << "=";
        else if (i == pos) std::cout << ">";
        else std::cout << " ";
    }
    std::cout << "] (" << m_modules_compiled << "/" << m_total_modules << ") "
              << "Compiling: " << current_file << "\r";
    std::cout.flush();
}

std::string CompilerDriver::read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool CompilerDriver::compile(const std::string& root_file_path) {
    // 1. Reset state for a fresh compilation run.
    m_had_error = false;
    m_modules_compiled = 0;
    m_total_modules = 0; // Will be incremented by resolveModule
    m_module_cache.clear();
    m_compilation_stack.clear();
    m_compiled_c_files.clear();

    // 2. Recursively compile the root module and all of its dependencies.
    //    resolveModule is the heart of the compilation process.
    //    The Token here is a dummy, as the root file isn't imported by anything.
    auto root_module_type = resolveModule(root_file_path, Token());

    // 3. Check if any stage of the recursive compilation failed.
    if (!root_module_type || m_had_error) {
        std::cout << "\n" << BOLD << RED << "Build failed." << RESET << std::endl;
        return false;
    }

    // 4. After all files are compiled, perform the final "linker" check to
    //    ensure the root module provides a valid `main` function.
    auto main_symbol_it = root_module_type->exports.find("main");
    if (main_symbol_it == root_module_type->exports.end()) {
        std::cerr << "\n" << BOLD << RED << "Linker Error: " << RESET
                  << "Program has no exported 'main' function to act as an entry point.\n"
                  << "Required signature: 'export func main() -> i64' or 'export func main(args as list<string>) -> i64'."
                  << std::endl;
        return false;
    }

    // 5. Validate the signature of the found 'main' function.
    if (main_symbol_it->second->kind != TypeKind::FUNCTION) {
        std::cerr << "\n" << BOLD << RED << "Linker Error: " << RESET << "The global symbol 'main' must be a function." << std::endl;
        return false;
    }
    auto main_func_type = std::dynamic_pointer_cast<FunctionType>(main_symbol_it->second);
    if (!isInteger(main_func_type->return_type)) {
        std::cerr << "\n" << BOLD << RED << "Linker Error: " << RESET << "'main' function must be declared to return an integer type (e.g., i64), but it returns '" << main_func_type->return_type->toString() << "'." << std::endl;
        return false;
    }
    if (main_func_type->param_types.size() > 1 ||
        (main_func_type->param_types.size() == 1 && main_func_type->param_types[0]->toString() != "list<string>")) {
         std::cerr << "\n" << BOLD << RED << "Linker Error: " << RESET << "'main' function can only have zero parameters, or one parameter of type 'list<string>'." << std::endl;
         return false;
    }

    // 6. If all checks passed, link all the generated .c files into the final executable.
    log_step("Linking final executable...");
    std::string base_name = get_base_name(root_file_path);
    std::string runtime_path = "src/runtime/angara_runtime.c"; // Adjust this path as needed

    std::stringstream command_ss;
    command_ss << "gcc -o " << base_name;
    for (const auto& c_file : m_compiled_c_files) {
        command_ss << " " << c_file;
    }
    command_ss << " " << runtime_path << " -I. -Isrc/runtime -pthread -lm";
    std::string command = command_ss.str();

    std::cout << YELLOW << "   $ " << command << RESET << std::endl;
    int result = system(command.c_str());

    if (result != 0) {
        std::cerr << "\n" << RED << "Build failed: The C compiler/linker returned a non-zero exit code." << RESET << std::endl;
        return false;
    }

    m_modules_compiled = m_total_modules > 0 ? m_total_modules : 1;
    print_progress("Done!");
    std::cout << "\n" << BOLD << GREEN << "Build successful! Executable created: ./" << base_name << RESET << std::endl;
    return true;
}

std::string CompilerDriver::get_base_name(const std::string& path) {
    // Find the position of the last directory separator ('/' or '\')
    size_t last_slash = path.find_last_of("/\\");

    // If a separator is found, the substring starts after it. Otherwise, start at the beginning.
    size_t start = (last_slash == std::string::npos) ? 0 : last_slash + 1;

    // Find the position of the last dot (for the file extension)
    size_t last_dot = path.find_last_of('.');

    // If there's no dot, or the dot is before the last slash (e.g., "a.b/c"),
    // then the substring goes to the end of the string.
    if (last_dot == std::string::npos || last_dot < start) {
        last_dot = path.length();
    }

    // Return the substring between the start and the last dot.
    return path.substr(start, last_dot - start);
}


std::shared_ptr<ModuleType> CompilerDriver::resolveModule(const std::string& path, const Token& import_token) {
    // 1. Check the cache first.
    if (m_module_cache.count(path)) {
        return m_module_cache[path];
    }

    // 2. Check for circular dependencies.
    for (const auto& p : m_compilation_stack) {
        if (p == path) {
            std::cerr << "\nError at line " << import_token.line << ": Circular dependency detected involving module '" << path << "'.\n";
            m_had_error = true;
            return nullptr;
        }
    }

    m_compilation_stack.push_back(path);
    m_total_modules++;
    print_progress(path);

    // 3. Read the source file.
    std::string source = read_file(path);
    if (source.empty()) {
        std::cerr << "\nError at line " << import_token.line << ": Could not open source file '" << path << "'\n";
        m_had_error = true;
        m_compilation_stack.pop_back();
        return nullptr;
    }

    // --- THIS IS THE FIX ---

    // 4. --- RUN THE FULL PIPELINE for the dependency ---
    ErrorHandler errorHandler(source);

    Lexer lexer(source);
    auto tokens = lexer.scanTokens();

    Parser parser(tokens, errorHandler);
    auto statements = parser.parseStmts();
    if (errorHandler.hadError()) {
        m_had_error = true;
        m_compilation_stack.pop_back();
        return nullptr;
    }

    // Correctly create the TypeChecker, passing the module name.
    std::string module_name = get_base_name(path);
    TypeChecker typeChecker(*this, errorHandler, module_name);
    if (!typeChecker.check(statements)) {
        m_had_error = true;
        m_compilation_stack.pop_back();
        return nullptr;
    }

    // 5. --- TRANSPILE ---
    CTranspiler transpiler(typeChecker, errorHandler);
    auto [header_code, source_code] = transpiler.generate(statements, module_name, m_compiled_module_names);

    if (m_had_error || (header_code.empty() && source_code.empty())) {
         m_compilation_stack.pop_back();
         return nullptr;
    }

    // 6. --- WRITE FILES TO DISK ---
    std::string h_filename = module_name + ".h";
    std::ofstream h_file(h_filename);
    h_file << header_code;
    h_file.close();

    std::string c_filename = module_name + ".c";
    std::ofstream c_file(c_filename);
    c_file << source_code;
    c_file.close();

    // 7. --- GET THE PUBLIC API for the CALLER ---
    // The getModuleType() method returns the completed module interface.
    auto module_type = typeChecker.getModuleType();

    // --- END OF FIX ---

    m_compilation_stack.pop_back();
    m_module_cache[path] = module_type;
    m_compiled_c_files.push_back(c_filename);
    m_compiled_module_names.push_back(module_name); // <-- ADD THIS

    return module_type;
}

} // namespace angara