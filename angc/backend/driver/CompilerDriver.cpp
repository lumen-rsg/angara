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

#include "../../runtime/angara_runtime.h"

const char* const RESET = "\033[0m";
const char* const BOLD = "\033[1m";
const char* const RED = "\033[31m";
const char* const GREEN = "\033[32m";
const char* const YELLOW = "\033[33m";

typedef const AngaraFuncDef* (*AngaraModuleInitFn)(int*);

namespace angara {

    class TypeStringParser {
    public:
        // The parser is initialized with the full source string and a map of
        // class names that have been discovered in the current module.
        TypeStringParser(const std::string& str,
                         std::map<std::string, std::shared_ptr<ClassType>>& known_classes)
                : m_source(str), m_known_classes(known_classes) {}

        // --- Public Parser Interface ---

        // This is the primary method called by the driver. It parses a single,
        // complete type signature from the current position in the string.
        std::shared_ptr<Type> parse_single_type() {
            return parse_optional();
        }

        // --- Public Utilities for the Driver ---

        bool is_at_end() {
            return m_current >= m_source.length();
        }

        char peek() {
            return is_at_end() ? '\0' : m_source[m_current];
        }

        void consume(char expected) {
            if (is_at_end()) {
                throw std::runtime_error("Unexpected end of type string. Expected '" + std::string(1, expected) + "'.");
            }
            char found = m_source[m_current++];
            if (found != expected) {
                throw std::runtime_error("Expected '" + std::string(1, expected) + "' but found '" + std::string(1, found) + "'.");
            }
        }

        void consume_variadic() {
            consume('.');
            consume('.');
            consume('.');
        }

    private:
        // --- Internal Parser Grammar Rules ---

        // An optional type is a base type followed by an optional '?'.
        std::shared_ptr<Type> parse_optional() {
            auto base_type = parse_base();
            if (!is_at_end() && peek() == '?') {
                consume('?');
                return std::make_shared<OptionalType>(base_type);
            }
            return base_type;
        }

        // A base type is a primitive, a class name, a list, or a record.
        std::shared_ptr<Type> parse_base() {
            if (is_at_end()) {
                throw std::runtime_error("Unexpected end of type string.");
            }

            char c = peek();

            if (isupper(c)) {
                std::string class_name;
                while (isalnum(peek()) || peek() == '_') {
                    class_name += m_source[m_current++];
                }
                if (m_known_classes.count(class_name)) {
                    return std::make_shared<InstanceType>(m_known_classes.at(class_name));
                }
                throw std::runtime_error("Unknown class name '" + class_name + "' in type string.");
            }

            m_current++; // Consume the single-character type token
            switch (c) {
                case 'i': return std::make_shared<PrimitiveType>("i64");
                case 'd': return std::make_shared<PrimitiveType>("f64");
                case 's': return std::make_shared<PrimitiveType>("string");
                case 'b': return std::make_shared<PrimitiveType>("bool");
                case 'a': return std::make_shared<AnyType>();
                case 'n': return std::make_shared<NilType>();
                case 'l': {
                    consume('<');
                    auto element_type = parse_optional();
                    consume('>');
                    return std::make_shared<ListType>(element_type);
                }
                case '{': {
                    consume('}');
                    return std::make_shared<RecordType>(std::map<std::string, std::shared_ptr<Type>>{});
                }
                default:
                    throw std::runtime_error("Invalid type character '" + std::string(1, c) + "' in type string.");
            }
        }

        std::string m_source;
        size_t m_current = 0;
        std::map<std::string, std::shared_ptr<ClassType>>& m_known_classes;
    };

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
        std::cout << ss.str() << "\r\033[K" << std::flush;
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
        command_ss << " -Wl,-rpath," << m_native_module_path;
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
        // 1. Find the absolute path to the module file.
        std::string found_path;
        bool is_direct_path = path_or_id.find('/') != std::string::npos || path_or_id.find('\\') != std::string::npos;

        if (is_direct_path || path_or_id.ends_with(".an") || path_or_id.ends_with(".so") || path_or_id.ends_with(".dylib") || path_or_id.ends_with(".dll")) {
            // It's a direct path.
            if (std::filesystem::exists(path_or_id)) {
                found_path = std::filesystem::absolute(path_or_id).string();
            }
        } else {
            // It's a logical name, like "json" or "fs". Search for it.
            const std::vector<std::string> search_paths = { ".", m_angara_module_path, m_native_module_path };
            for (const auto& dir : search_paths) {
                // Check for Angara source file: e.g., ./json.an or /opt/src/angara/modules/json.an
                std::filesystem::path an_path = std::filesystem::path(dir) / (path_or_id + ".an");
                if (std::filesystem::exists(an_path)) {
                    found_path = std::filesystem::absolute(an_path).string();
                    break;
                }

                // Check for native library: e.g., ./libfs.so or /opt/modules/angara/libfs.so
                std::filesystem::path so_path = std::filesystem::path(dir) / ("lib" + path_or_id + ".so");
                if (std::filesystem::exists(so_path)) {
                    found_path = std::filesystem::absolute(so_path).string();
                    break;
                }

                // Add other native extensions if needed (.dylib, .dll)
                std::filesystem::path dylib_path = std::filesystem::path(dir) / ("lib" + path_or_id + ".dylib");
                if (std::filesystem::exists(dylib_path)) {
                    found_path = std::filesystem::absolute(dylib_path).string();
                    break;
                }
            }
        }

        if (found_path.empty()) {
            std::cerr << "\n" << BOLD << RED << "Error at line " << import_token.line << RESET << ": Cannot find module '" << path_or_id << "'.\n";
            m_had_error = true;
            return nullptr;
        }

        // 2. Use the canonical, absolute path as the cache key.
        const std::string& cache_key = found_path;
        if (m_module_cache.count(cache_key)) {
            return m_module_cache[cache_key];
        }

        // 3. Check for circular dependencies.
        for (const auto& p : m_compilation_stack) {
            if (p == cache_key) {
                std::cerr << "\n" << BOLD << RED << "Error at line " << import_token.line << RESET << ": Circular dependency detected for module '" << path_or_id << "'.\n";
                m_had_error = true;
                return nullptr;
            }
        }

        // 4. Now that we have the path, compile or load it.
        m_compilation_stack.push_back(cache_key);
        m_total_modules++;
        print_progress(path_or_id);

        std::shared_ptr<ModuleType> module_type = nullptr;
        if (found_path.ends_with(".so") || found_path.ends_with(".dylib") || found_path.ends_with(".dll")) {
            module_type = loadNativeModule(found_path, import_token);
            if (module_type) {
                m_native_lib_paths.insert(std::filesystem::path(found_path).parent_path().string());
                m_native_lib_names.push_back(get_base_name(found_path));
                module_type->is_native = true;
            }
        } else {

            // --- Compile an Angara source file ---
            m_compiled_angara_files.push_back(found_path);
            std::string source = read_file(found_path);

            int line_count = 1;
            for(char c : source) { if (c == '\n') line_count++; }
            m_line_counts[found_path] = line_count;

            ErrorHandler errorHandler(source);
            Lexer lexer(source);
            auto tokens = lexer.scanTokens();
            Parser parser(tokens, errorHandler);
            auto statements = parser.parseStmts();
            if (errorHandler.hadError()) { m_had_error = true; m_compilation_stack.pop_back(); return nullptr; }

            std::string module_name = get_base_name(found_path);
            TypeChecker typeChecker(*this, errorHandler, module_name);
            if (!typeChecker.check(statements)) { m_had_error = true; m_compilation_stack.pop_back(); return nullptr; }
            auto module_type_obj = typeChecker.getModuleType();
            m_angara_module_names.push_back(module_name);
            CTranspiler transpiler(typeChecker, errorHandler);
            auto [header_code, source_code] = transpiler.generate(statements, module_type_obj, m_angara_module_names);
            if (errorHandler.hadError()) { m_had_error = true; m_compilation_stack.pop_back(); return nullptr; }

            std::string h_filename = module_name + ".h";
            m_compiled_h_files.push_back(h_filename);
            std::ofstream h_file(h_filename);
            h_file << header_code;

            std::string c_filename = module_name + ".c";
            m_compiled_c_files.push_back(c_filename);
            std::ofstream c_file(c_filename);
            c_file << source_code;

            module_type = typeChecker.getModuleType();
        }

        // 5. Clean up, cache the result, and return.
        m_compilation_stack.pop_back();
        if (module_type) {
            m_module_cache[cache_key] = module_type;
            m_modules_compiled++;
        }
        return module_type;
    }


    std::shared_ptr<ModuleType> CompilerDriver::loadNativeModule(const std::string& path, const Token& import_token) {
        print_progress("Loading native module: " + path);

        void* handle = dlopen(path.c_str(), RTLD_LAZY);
        if (!handle) {
            std::cerr << "\n" << BOLD << RED << "Error at line " << import_token.line << RESET
                      << ": Could not load native module '" << path << "'. Reason: " << dlerror() << "\n";
            m_had_error = true;
            return nullptr;
        }

        std::string module_name = get_base_name(path);
        std::string init_func_name = "Angara_" + module_name + "_Init";

        // The init function returns a simple array of AngaraFuncDef.
        typedef const AngaraFuncDef* (*AngaraModuleInitFn)(int*);
        auto init_fn = (AngaraModuleInitFn)dlsym(handle, init_func_name.c_str());

        if (!init_fn) {
            std::cerr << "\n" << BOLD << RED << "Error at line " << import_token.line << RESET
                      << ": Invalid native module '" << path << "'. Missing entry point: " << init_func_name << "\n";
            m_had_error = true;
            dlclose(handle);
            return nullptr;
        }

        int def_count = 0;
        const AngaraFuncDef* defs = init_fn(&def_count);

        auto module_type = std::make_shared<ModuleType>(module_name);
        module_type->is_native = true;

        // A map to hold all native classes discovered in this module.
        std::map<std::string, std::shared_ptr<ClassType>> native_classes;

        // --- Pass 1: Discover all CLASS definitions ---
        for (int i = 0; i < def_count; i++) {
            const AngaraFuncDef& func_def = defs[i];
            if (func_def.constructs) {
                const AngaraClassDef* class_def = func_def.constructs;
                if (native_classes.count(class_def->name)) { /* error: duplicate class */ continue; }

                auto class_type = std::make_shared<ClassType>(class_def->name);
                class_type->is_native = true;
                native_classes[class_def->name] = class_type;

                // Export the ClassType itself so it can be used in annotations.
                module_type->exports[class_def->name] = class_type;
            }
        }

        // Iterate through all exported functions in the module.
        for (int i = 0; i < def_count; i++) {
            const AngaraFuncDef& func_def = defs[i];
            if (!func_def.name || !func_def.type_string) continue;

            try {
                TypeStringParser parser(func_def.type_string, native_classes);

                // 1. Parse all parameter types.
                std::vector<std::shared_ptr<Type>> params;
                bool is_variadic = false;
                while (!parser.is_at_end() && parser.peek() != '-') {
                    params.push_back(parser.parse_single_type());
                    if (!parser.is_at_end() && parser.peek() == '.') {
                        parser.consume_variadic();
                        is_variadic = true;
                        if (!parser.is_at_end()) {
                            throw std::runtime_error("Variadic '...' must be at the end of the parameter list.");
                        }
                        break;
                    }
                }

                // 2. Consume the '->' arrow.
                parser.consume('-');
                parser.consume('>');

                // 3. Parse the return type.
                auto return_type = parser.parse_single_type();

                if (!parser.is_at_end()) {
                    throw std::runtime_error("Unexpected characters after return type in signature '" + std::string(func_def.type_string) + "'.");
                }

                // 4. If it's a constructor, populate the class methods and fields.
                if (func_def.constructs) {
                    const AngaraClassDef* class_def = func_def.constructs;
                    auto class_type = native_classes.at(class_def->name);

                    if (class_def->methods) {
                        for (int m = 0; class_def->methods[m].name != NULL; ++m) {
                            const AngaraMethodDef& method_def = class_def->methods[m];
                            if (!method_def.name || !method_def.type_string) continue;

                            // --- Create a NEW, SEPARATE parser for the method's signature ---
                            TypeStringParser method_parser(method_def.type_string, native_classes);

                            std::vector<std::shared_ptr<Type>> method_params;
                            while (!method_parser.is_at_end() && method_parser.peek() != '-') {
                                method_params.push_back(method_parser.parse_single_type());
                            }
                            method_parser.consume('-');
                            method_parser.consume('>');
                            auto method_return_type = method_parser.parse_single_type();

                            if (!method_parser.is_at_end()) {
                                throw std::runtime_error("Unexpected characters after return type in signature for method '" + std::string(method_def.name) + "'.");
                            }

                            auto method_type = std::make_shared<FunctionType>(method_params, method_return_type);
                            class_type->methods[method_def.name] = {method_type, AccessLevel::PUBLIC, Token(), false};
                        }
                    }

                    // Populate the class's fields.
                    if (class_def->fields) {
                        for (int f = 0; class_def->fields[f].name != NULL; ++f) {
                            const AngaraFieldDef& field_def = class_def->fields[f];
                            if (!field_def.name || !field_def.type_string) continue;

                            TypeStringParser field_parser(field_def.type_string, native_classes);
                            auto field_type = field_parser.parse_single_type();
                            if (!field_parser.is_at_end()) {
                                throw std::runtime_error("Unexpected characters in field type string for '" + std::string(field_def.name) + "'.");
                            }
                            class_type->fields[field_def.name] = {field_type, AccessLevel::PUBLIC, Token(), field_def.is_const};
                        }
                    }
                }


                auto func_type = std::make_shared<FunctionType>(params, return_type, is_variadic);
                module_type->exports[func_def.name] = func_type;

            } catch (const std::runtime_error& e) {
                std::cerr << "\n" << BOLD << YELLOW << "Warning:" << RESET << " Could not parse ABI definition for '"
                          << (func_def.name ? func_def.name : "unknown")
                          << "' in module '" << path << "': " << e.what() << "\n";
            }
        }

        m_modules_compiled++;
        return module_type;
    }


} // namespace angara