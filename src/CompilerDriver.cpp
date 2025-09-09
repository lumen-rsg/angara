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

#include <dlfcn.h> // For dlopen, dlsym
#include <filesystem>
#include <thread>

#include "AngaraABI.h"

const char* const RESET = "\033[0m";
const char* const BOLD = "\033[1m";
const char* const RED = "\033[31m";
const char* const GREEN = "\033[32m";
const char* const YELLOW = "\033[33m";

typedef const AngaraFuncDef* (*AngaraModuleInitFn)(int*);

namespace angara {

    void CompilerDriver::log_step(const std::string& message) {
        // 1. Clear the current line (which has the progress bar on it).
        std::cout << "\r\033[K";

        // 2. Print the log message on its own line.
        std::cout << BOLD << GREEN << "-> " << RESET << BOLD << message << RESET << std::endl;

        // 3. Reprint the last known progress bar state on the new line.
        print_progress(m_last_progress_message);
    }

    CompilerDriver::CompilerDriver()
            : m_runtime_path("/opt/src/angara/runtime"),
              m_angara_module_path("/opt/src/angara/modules"),
              m_native_module_path("/opt/modules/angara/")
    {}

    void CompilerDriver::print_progress(const std::string& current_file) {
        // Store the message so other functions can reprint it.
        m_last_progress_message = current_file;

        int bar_width = 20;
        float progress = (m_total_modules > 0) ? (float)m_modules_compiled / m_total_modules : 0;
        // Don't let the bar go to 100% until the very end.
        if (m_modules_compiled == m_total_modules && current_file != "Done!") {
            progress = 0.99;
        }
        int pos = bar_width * progress;

        std::stringstream ss;
        ss << BOLD << GREEN << "[" << RESET;
        for (int i = 0; i < bar_width; ++i) {
            if (i < pos) ss << BOLD << GREEN << "=" << RESET;
            else if (i == pos && progress < 1.0) ss << BOLD << GREEN << ">" << RESET;
            else ss << " ";
        }
        ss << BOLD << GREEN << "] " << RESET << "(" << m_modules_compiled << "/" << m_total_modules << ") "
           << "Compiling: " << current_file;

        // \r moves to the beginning. \033[K clears the line.
        std::cout << "\r\033[K" << ss.str() << std::flush;
}

    static std::string get_lib_name(const std::string& path) {
        std::string basename = CompilerDriver::get_base_name(path);
        // Strip the "lib" prefix if it exists
        if (basename.rfind("lib", 0) == 0) {
            return basename.substr(3);
        }
        return basename;
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
    m_build_start_time = std::chrono::high_resolution_clock::now();
    // 1. Reset state for a fresh compilation run.
    m_had_error = false;
    m_modules_compiled = 0;
    m_total_modules = 1; // Will be incremented by resolveModule
    m_module_cache.clear();
    m_compilation_stack.clear();
    m_compiled_c_files.clear();

    // 2. Recursively compile the root module and all of its dependencies.
    //    resolveModule is the heart of the compilation process.
    //    The Token here is a dummy, as the root file isn't imported by anything.
    auto root_module_type = resolveModule(root_file_path, Token());

    // 3. Check if any stage of the recursive compilation failed.
    if (!root_module_type || m_had_error) {
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
    std::string runtime_c_path = std::filesystem::path(m_runtime_path) / "angara_runtime.c";

    std::stringstream command_ss;
    command_ss << "gcc -o " << base_name;

    // Add our generated source files
    for (const auto& c_file : m_compiled_c_files) {
        command_ss << " " << c_file;
    }
    command_ss << " " << runtime_c_path;

    // Add include paths
    command_ss << " -I. -I" << m_runtime_path;

    // Add library search paths (includes the standard path and any relative ones)
    m_native_lib_paths.insert(m_native_module_path);
    for (const auto& lib_path : m_native_lib_paths) {
        command_ss << " -L" << lib_path;
    }

    // Add libraries to link
    for (const auto& lib_name : m_native_lib_names) {
        command_ss << " -l" << lib_name;
    }

    // Add final flags
    command_ss << " -pthread -lm";
    std::string command = command_ss.str();

        // --- NEW LOGIC: Redirect output and conditionally print ---
        std::string temp_log_file = "angara_build.log";
        std::string redirected_command = command + " > " + temp_log_file + " 2>&1";

        int result = system(redirected_command.c_str());

        if (result != 0) {
            std::cout << "\r\033[K"; // Clear the progress bar line
            std::cerr << BOLD << RED << "\nBuild failed." << RESET << " The system compiler returned an error." << std::endl;

            // Read the contents of the log file to show the user the GCC error.
            std::string gcc_output = read_file(temp_log_file);
            if (!gcc_output.empty()) {
                std::cerr << "\n" << YELLOW << "--- Compiler Output ---" << RESET << std::endl;
                std::cerr << gcc_output;
                std::cerr << YELLOW << "--- End Compiler Output ---" << RESET << std::endl;
            }

            std::cerr << "\nThe command that failed was:\n" << "   $ " << command << std::endl;

            return false;
        }


        for (const auto& c_file : m_compiled_c_files) {
            remove(c_file.c_str());
        }
        for (const auto& h_file : m_compiled_h_files) {
            remove(h_file.c_str());
        }

        // On success, just clean up.
        remove(temp_log_file.c_str());

    m_modules_compiled = m_total_modules > 0 ? m_total_modules : 1;
    print_progress("Done!");
    std::cout << "\n" << BOLD << GREEN << "Executable created: ./" << base_name << RESET << std::endl;

    // Calculate build duration
    auto build_end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> build_duration = build_end_time - m_build_start_time;

    // Calculate total lines
    int total_lines = 0;
    for (const auto& [file, count] : m_line_counts) {
        total_lines += count;
    }

    // Print the summary
    std::cout << "\n" << BOLD << YELLOW << " -> Compilation Summary <- " << RESET << std::endl;
    std::cout << " \n    • " << "Modules Compiled: " << m_compiled_angara_files.size() << " (";
    for(size_t i = 0; i < m_compiled_angara_files.size(); ++i) {
        std::cout << get_base_name(m_compiled_angara_files[i]) << (i == m_compiled_angara_files.size() - 1 ? "" : ", ");
    }
    std::cout << ")" << std::endl;
    std::cout << "    • " << "Total Lines of Code: " << total_lines << std::endl;
    std::cout << "    • " << "Generated C Files: ";
    for(size_t i = 0; i < m_compiled_c_files.size(); ++i) {
        std::cout << m_compiled_c_files[i] << (i == m_compiled_c_files.size() - 1 ? "" : ", ");
    }
    std::cout << std::endl;
    std::cout << "    • " << "Build Time: " << std::fixed << std::setprecision(2) << build_duration.count() << "s" << std::endl;

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

    std::string basename = path.substr(start, last_dot - start);
    // If the name starts with "lib", strip it. e.g., "libfs" -> "fs"
    if (basename.rfind("lib", 0) == 0) {
        return basename.substr(3);
    }
    return basename;
}


std::shared_ptr<ModuleType> CompilerDriver::resolveModule(const std::string& path_or_id, const Token& import_token) {

    std::string found_path = "";
    bool is_direct_path = (path_or_id.find('/') != std::string::npos ||
                           path_or_id.find('\\') != std::string::npos ||
                           path_or_id.ends_with(".an") ||
                           path_or_id.ends_with(".so") ||
                           path_or_id.ends_with(".dylib") ||
                           path_or_id.ends_with(".dll"));

    if (is_direct_path) {
        // It's a relative or absolute path, use it directly.
        std::ifstream f(path_or_id);
        if (f.good()) {
            found_path = path_or_id;
        }
    } else {
        // It's a logical ID (like "fs" or "utils"). Search for it.
        std::vector<std::string> search_paths = {
                ".", // 1. Current directory
                m_angara_module_path, // 2. Standard Angara module path
                m_native_module_path  // 3. Standard native module path
        };
        for (const auto& dir : search_paths) {
            // Check for both .an and .so/.dylib
            std::vector<std::string> potential_paths = {
                    std::filesystem::path(dir) / (path_or_id + ".an"),
                    std::filesystem::path(dir) / ("lib" + path_or_id + ".so"),
                    std::filesystem::path(dir) / ("lib" + path_or_id + ".dylib")
            };
            for (const auto& p : potential_paths) {
                std::ifstream f(p);
                if (f.good()) {
                    found_path = p;
                    break;
                }
            }
            if (!found_path.empty()) break;
        }
    }

    if (found_path.empty()) {
        std::cerr << "\n" << BOLD << RED << "Error at line " << import_token.line << RESET << ": Cannot find module '" << path_or_id << "'.\n";
        m_had_error = true;
        return nullptr;
    }

    // 1. Check the cache first to avoid recompiling the same file.
    if (m_module_cache.count(path_or_id)) {
        return m_module_cache[path_or_id];
    }

    // 2. Check for circular dependencies by seeing if the path is already in our call stack.
    for (const auto& p : m_compilation_stack) {
        if (p == path_or_id) {
            std::cerr << "\n" << BOLD << RED << "Error at line " << import_token.line << RESET << ": Circular dependency detected. Module '" << path_or_id << "' is already being compiled in this chain.\n";
            m_had_error = true;
            return nullptr;
        }
    }

    m_compilation_stack.push_back(path_or_id);
    m_total_modules++;
    print_progress(path_or_id);

    // --- DISPATCHER: Decide how to handle the file based on its extension ---
    std::shared_ptr<ModuleType> module_type = nullptr;
    if (path_or_id.ends_with(".so") || path_or_id.ends_with(".dylib") || path_or_id.ends_with(".dll")) {
        // --- Path 1: Load a pre-compiled native module ---
        module_type = loadNativeModule(path_or_id, import_token);

        if (module_type) {
            // 1. Extract the directory path.
            size_t last_slash = path_or_id.find_last_of("/\\");
            if (last_slash != std::string::npos) {
                m_native_lib_paths.insert(path_or_id.substr(0, last_slash));
            }
            // 2. Extract the clean library name.
            m_native_lib_names.push_back(get_lib_name(path_or_id));
        }

    } else {
        // --- Path 2: Compile an Angara source file ---
        m_compiled_angara_files.push_back(path_or_id);
        std::string source = read_file(path_or_id);

        m_line_counts[path_or_id] = 1;
        for(char c : source) {
            if (c == '\n') m_line_counts[path_or_id]++;
        }

        if (source.empty()) {
            std::cerr << "\n" << BOLD << RED << "Error at line " << import_token.line << RESET << ": Could not open source file '" << path_or_id << "'\n";
            m_had_error = true;
            m_compilation_stack.pop_back();
            return nullptr;
        }

        // Run the full pipeline: Lexer -> Parser -> TypeChecker
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

        std::string module_name = get_base_name(path_or_id);
        TypeChecker typeChecker(*this, errorHandler, module_name);
        if (!typeChecker.check(statements)) {
            m_had_error = true;
            m_compilation_stack.pop_back();
            return nullptr;
        }

        // Transpile the type-checked AST to C header and source files.
        CTranspiler transpiler(typeChecker, errorHandler);
        auto [header_code, source_code] = transpiler.generate(statements, module_name, m_compiled_module_names);

        if (m_had_error || (header_code.empty() && source_code.empty())) {
             m_compilation_stack.pop_back();
             return nullptr;
        }

        // Write the generated files to disk.
        std::string h_filename = module_name + ".h";
        m_compiled_h_files.push_back(h_filename); // <-- ADD THIS LINE
        std::ofstream h_file(h_filename);
        h_file << header_code;
        h_file.close();

        std::string c_filename = module_name + ".c";
        std::ofstream c_file(c_filename);
        c_file << source_code;
        c_file.close();

        // Get the public API of the compiled module.
        module_type = typeChecker.getModuleType();
        m_compiled_c_files.push_back(c_filename);
    }

    // 3. Clean up and cache the result.
    m_compilation_stack.pop_back();
    if (module_type) {
        m_module_cache[path_or_id] = module_type;
        m_modules_compiled++;
    }

    return module_type;
}


    std::shared_ptr<ModuleType> CompilerDriver::loadNativeModule(const std::string& path, const Token& import_token) {
    print_progress("Loading native module: " + path);

    // 1. Open the dynamic library.
    void* handle = dlopen(path.c_str(), RTLD_LAZY);
    if (!handle) {
        std::cerr << "\nError at line " << import_token.line << ": Could not load native module '" << path << "'. Reason: " << dlerror() << "\n";
        m_had_error = true;
        return nullptr;
    }

        std::string module_name = get_base_name(path);
        std::string init_func_name = "Angara_" + module_name + "_Init";
        AngaraModuleInitFn init_fn = (AngaraModuleInitFn)dlsym(handle, init_func_name.c_str());
    if (!init_fn) {
        std::cerr << "\nError at line " << import_token.line << ": Invalid native module '" << path << "'. Missing " << init_func_name << " entry point.\n";
        m_had_error = true;
        dlclose(handle);
        return nullptr;
    }

    // 3. Call the init function to get the API definitions.
    int def_count = 0;
    const AngaraFuncDef* defs = init_fn(&def_count);

    auto module_type = std::make_shared<ModuleType>(module_name);

    // 4. Populate the module's public API.
    for (int i = 0; i < def_count; i++) {
        const AngaraFuncDef& def = defs[i];
        // For now, we don't have the types of the parameters, so we'll have to
        // represent them as 'any'. This is a limitation of a pure C ABI.
        // A more advanced ABI might have a type description string.
        std::vector<std::shared_ptr<Type>> params;
        if (def.arity != -1) { // -1 is variadic
             for (int j = 0; j < def.arity; ++j) {
                 params.push_back(std::make_shared<PrimitiveType>("any"));
             }
        }
        auto return_type = std::make_shared<PrimitiveType>("any");
        auto func_type = std::make_shared<FunctionType>(params, return_type, def.arity == -1);

        module_type->is_native = true;

        module_type->exports[def.name] = func_type;
    }

    // We don't need to add this to a link list. The OS linker will handle it.
    // We just needed the type information.
    // dlclose(handle); // Don't close it, we need the symbols at runtime!

    m_module_cache[path] = module_type;
    m_modules_compiled++; // Count it as a "compiled" unit
    return module_type;
}

} // namespace angara