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
    m_modules_compiled = 0;

    auto root_module_type = resolveModule(root_file_path, Token());

    if (!root_module_type || m_had_error) {
        std::cout << "\n" << BOLD << RED << "Build failed." << RESET << std::endl;
        return false;
    }

    log_step("Linking final executable...");
    std::string base_name = get_base_name(root_file_path);
    std::string runtime_path = "src/runtime/angara_runtime.c";

    std::stringstream command_ss;
    command_ss << "gcc -o " << base_name;
    // Add all the generated .c files to the command
    for (const auto& c_file : m_compiled_c_files) {
        command_ss << " " << c_file;
    }
    command_ss << " " << runtime_path << " -I. -Isrc/runtime -pthread -lm";
    std::string command = command_ss.str();

    std::cout << YELLOW << "   $ " << command << RESET << std::endl;
    int result = system(command.c_str());

    if (result != 0) {
        std::cerr << "\n" << RED << "Build failed: C linker returned a non-zero exit code." << RESET << std::endl;
        return false;
    }

    m_modules_compiled = m_total_modules;
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

    void extract_public_api(TypeChecker& checker, std::shared_ptr<ModuleType>& module_type) {
    const auto& global_scope = checker.getSymbolTable().getGlobalScope();
    for (const auto& [name, symbol] : global_scope) {
        // A simplified rule: export all global functions, classes, traits, and consts.
        if (symbol->is_const || symbol->type->kind == TypeKind::FUNCTION ||
            symbol->type->kind == TypeKind::CLASS || symbol->type->kind == TypeKind::TRAIT) {
            module_type->exports[name] = symbol->type;
            }
    }
}

std::shared_ptr<ModuleType> CompilerDriver::resolveModule(const std::string& path, const Token& import_token) {
    if (m_module_cache.count(path)) {
        return m_module_cache[path];
    }
    for (const auto& p : m_compilation_stack) {
        if (p == path) {
            // Error reporting needs a real ErrorHandler for the importing file
            std::cerr << "\nError: Circular dependency detected involving '" << path << "'.\n";
            m_had_error = true;
            return nullptr;
        }
    }

    m_compilation_stack.push_back(path);
    m_total_modules++; // We can adjust this later for a more accurate progress bar
    print_progress(path);

    std::string source = read_file(path);
    if (source.empty()) {
        std::cerr << "\nError: Could not open source file '" << path << "'.\n";
        m_had_error = true;
        m_compilation_stack.pop_back();
        return nullptr;
    }

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

    TypeChecker typeChecker(*this, errorHandler);
    if (!typeChecker.check(statements)) {
        m_had_error = true;
        m_compilation_stack.pop_back();
        return nullptr;
    }

    std::string module_name = get_base_name(path);
    CTranspiler transpiler(typeChecker, errorHandler);
    auto [header_code, source_code] = transpiler.generate(statements, module_name);

    if (header_code.empty() && source_code.empty()) {
        m_had_error = true;
        m_compilation_stack.pop_back();
        return nullptr;
    }

    std::string h_filename = module_name + ".h";
    std::ofstream h_file(h_filename);
    h_file << header_code;
    h_file.close();

    std::string c_filename = module_name + ".c";
    std::ofstream c_file(c_filename);
    c_file << source_code;
    c_file.close();

    auto module_type = std::make_shared<ModuleType>(module_name);
    extract_public_api(typeChecker, module_type);

    m_compilation_stack.pop_back();
    m_module_cache[path] = module_type;
    m_compiled_c_files.push_back(c_filename); // Add to the list for the final link

    return module_type;
}

} // namespace angara