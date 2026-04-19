//
// Created by cv2 on 07.09.2025.
//

#include "CompilerDriver.h"
#include "ErrorHandler.h"
#include "Lexer.h"
#include "Parser.h"
#include "TypeChecker.h"
#include "LLVMBackend.h"
#include "AngaraABI.h"
#include "Colors.h"
#include <iostream>
#include <fstream>
#include <sstream>

#include <dlfcn.h> // For dlopen, dlsym
#include <filesystem>

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
        std::cout << CLR_BOLD << CLR_GREEN << "-> " << CLR_RESET << CLR_BOLD << message << CLR_RESET << std::endl;

        // 3. Reprint the last known progress bar state on the new line.
        print_progress(m_last_progress_message);
    }

    CompilerDriver::CompilerDriver() {
        // Defaults, but BuildSystem should override these via set_paths
        m_angara_module_path = ".";
        m_native_module_path = ".";
    }

    void CompilerDriver::set_paths(std::string std_lib_path, std::string native_lib_path) {
        m_angara_module_path = std::move(std_lib_path);
        m_native_module_path = std::move(native_lib_path);
    }

    const std::set<std::string>& CompilerDriver::get_generated_object_files() const {
        return m_generated_object_files;
    }

    const std::vector<std::string>& CompilerDriver::get_native_libs_linked() const {
        return m_native_lib_names;
    }

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
        ss << CLR_BOLD << CLR_GREEN << "[" << CLR_RESET;
        for (int i = 0; i < bar_width; ++i) {
            if (i < pos) ss << CLR_BOLD << CLR_GREEN << "=" << CLR_RESET;
            else if (i == pos && progress < 1.0) ss << CLR_BOLD << CLR_GREEN << ">" << CLR_RESET;
            else ss << " ";
        }
        ss << CLR_BOLD << CLR_GREEN << "] " << CLR_RESET << "(" << m_modules_compiled << "/" << m_total_modules << ") "
           << "Compiling: " << current_file;

        // \r moves to the beginning. \033[K clears the line.
        std::cout << ss.str() << "\r\033[K" << std::flush;
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

    bool CompilerDriver::compile(const ProjectConfig& project, const std::string& root_file_path) {
        m_build_start_time = std::chrono::high_resolution_clock::now();
        m_had_error = false;
        m_modules_compiled = 0;
        m_total_modules = 1;
        m_module_cache.clear();
        m_compilation_stack.clear();
        m_generated_object_files.clear();
        m_native_lib_names.clear();

        // 1. Kick off the recursive resolution.
        // This triggers resolveModule -> loadNative/compileAngara recursively.
        auto root_module = resolveModule(root_file_path, Token());

        if (!root_module || m_had_error) {
            return false;
        }

        // 2. Print build timing.
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - m_build_start_time);
        double seconds = duration.count() / 1000.0;
        int total_modules = static_cast<int>(m_generated_object_files.size());
        int native_count = static_cast<int>(m_native_lib_names.size());

        std::cout << CLR_BOLD << CLR_GREEN << "✓ " << CLR_RESET << "Compiled "
                  << total_modules << " module" << (total_modules != 1 ? "s" : "")
                  << (native_count > 0 ? " + " + std::to_string(native_count) + " native lib" + (native_count != 1 ? "s" : "") : "")
                  << " in " << CLR_BOLD << seconds << "s" << CLR_RESET << std::endl;

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
        // If the name starts with "lib", strip it. e.g., "libfs" -> "std::filesystem"
        if (basename.rfind("lib", 0) == 0) {
            return basename.substr(3);
        }
        return basename;
    }

    static std::optional<std::string> find_candidate(const std::filesystem::path& dir, const std::string& name) {
        std::error_code ec;

        // If the name already has an extension, check it directly
        if (std::filesystem::path(name).has_extension()) {
            std::filesystem::path p = dir / name;
            if (std::filesystem::exists(p, ec) && !std::filesystem::is_directory(p, ec)) {
                return std::filesystem::canonical(p).string();
            }
        }

        // 1. Try Angara Source (.an)
        std::filesystem::path an_path = dir / (name + ".an");
        if (std::filesystem::exists(an_path, ec) && !std::filesystem::is_directory(an_path, ec)) {
            return std::filesystem::canonical(an_path).string();
        }

        // 2. Try Native Libraries (.so, .dylib)
        const std::vector<std::string> native_exts = { ".so", ".dylib", ".dll" };
        for (const auto& ext : native_exts) {
            std::filesystem::path p = dir / (name + ext);
            if (std::filesystem::exists(p, ec) && !std::filesystem::is_directory(p, ec)) return std::filesystem::canonical(p).string();

            std::filesystem::path lib_p = dir / ("lib" + name + ext);
            if (std::filesystem::exists(lib_p, ec) && !std::filesystem::is_directory(lib_p, ec)) return std::filesystem::canonical(lib_p).string();
        }

        return std::nullopt;
    }

    std::shared_ptr<ModuleType> CompilerDriver::resolveModule(const std::string& path_or_id, const Token& import_token) {
    namespace fs = std::filesystem;
    std::string found_path = "";
    std::string module_name = ""; // We will determine this carefully

    // 1. Determine Search Context
    fs::path base_dir = fs::current_path();
    if (import_token.file && !import_token.file->empty()) {
        base_dir = fs::path(*import_token.file).parent_path();
    }

    // ==========================================================
    // PHASE A: Discovery (Find the physical file)
    // ==========================================================

    // Check if it's an absolute path already
    fs::path input_path(path_or_id);
    if (input_path.is_absolute()) {
        std::error_code ec;
        if (fs::exists(input_path, ec)) found_path = fs::canonical(input_path, ec).string();
    }

    if (found_path.empty()) {
        bool is_relative = (path_or_id.find("./") == 0 || path_or_id.find("../") == 0);

        if (is_relative) {
            if (auto p = find_candidate(base_dir, path_or_id)) found_path = *p;
        } else {
            // Priority 1: Exact Project Name
            if (m_project_entries.count(path_or_id)) {
                found_path = m_project_entries.at(path_or_id);
            }
            // Priority 2: Local Sibling
            if (found_path.empty()) {
                if (auto p = find_candidate(base_dir, path_or_id)) found_path = *p;
            }
            // Priority 3: Internal Project Files (cross-project)
            if (found_path.empty()) {
                for (auto const& [name, entry_file] : m_project_entries) {
                    fs::path proj_dir = fs::path(entry_file).parent_path();
                    if (auto p = find_candidate(proj_dir, path_or_id)) {
                        found_path = *p;
                        break;
                    }
                }
            }
            // Priority 4: StdLib (Source)
            if (found_path.empty()) {
                if (auto p = find_candidate(fs::path(m_angara_module_path), path_or_id)) found_path = *p;
            }
            // Priority 5: Native Modules (Binary)
            if (found_path.empty()) {
                if (auto p = find_candidate(fs::path(m_native_module_path), path_or_id)) found_path = *p;
            }
        }
    }

    if (found_path.empty()) {
        std::string loc = (import_token.file) ? *import_token.file : "entry point";
        std::cerr << "Error: Module '" << path_or_id << "' not found (imported from " << loc << ")\n";
        m_had_error = true;
        return nullptr;
    }

    // ==========================================================
    // PHASE B: Identification (Determine the UNIQUE Module Name)
    // ==========================================================

    // 1. Does this path match a known Project Entry Point?
    for (auto const& [projName, entryPath] : m_project_entries) {
        if (found_path == entryPath) {
            module_name = projName;
            break;
        }
    }

    // 2. If it's a file inside a project folder but NOT the entry point
    if (module_name.empty()) {
        for (auto const& [projName, entryPath] : m_project_entries) {
            fs::path proj_dir = fs::path(entryPath).parent_path();
            if (found_path.find(proj_dir.string()) == 0) {
                // It's a helper file. Combine Project + Filename for uniqueness
                // e.g., "RabbitMQHelper_utils"
                module_name = projName + "_" + get_base_name(found_path);
                break;
            }
        }
    }

    // 3. Fallback to filename (StdLib or unmanaged files)
    if (module_name.empty()) {
        module_name = get_base_name(found_path);
    }

    // 4. CRITICAL: Never allow "main" as a module name
    // If we are left with "main", it means it's a single file app.
    if (module_name == "main") {
        module_name = "app_main";
    }

    // ==========================================================
    // PHASE C: Compilation and Caching
    // ==========================================================

    if (m_module_cache.count(found_path)) return m_module_cache[found_path];

    for (const auto& s : m_compilation_stack) {
        if (s == found_path) {
            std::cerr << "Error: Circular dependency: " << found_path << "\n";
            m_had_error = true;
            return nullptr;
        }
    }

    m_compilation_stack.push_back(found_path);
    std::shared_ptr<ModuleType> result = nullptr;

    if (found_path.ends_with(".so") || found_path.ends_with(".dylib") || found_path.ends_with(".dll")) {
        result = loadNativeModule(found_path, import_token);
        if (result) m_native_lib_names.push_back(get_base_name(found_path));
    } else {
        // Pass our carefully calculated module_name to the compiler
        result = compileAngaraSource(found_path, module_name);
    }

    m_compilation_stack.pop_back();
    if (result) m_module_cache[found_path] = result;

    return result;
}

    std::shared_ptr<ModuleType> CompilerDriver::compileAngaraSource(
    const std::string& path,
    const std::string& module_name
) {
        std::string source = read_file(path);
        auto filename_ptr = std::make_shared<std::string>(path);

        ErrorHandler errorHandler(source);

        Lexer lexer(source, filename_ptr, errorHandler);
        auto tokens = lexer.scanTokens();
        if (errorHandler.hadError()) { errorHandler.printSummary(); m_had_error = true; return nullptr; }

        Parser parser(tokens, errorHandler);
        auto statements = parser.parseStmts();
        if (errorHandler.hadError()) { errorHandler.printSummary(); m_had_error = true; return nullptr; }

        TypeChecker typeChecker(*this, errorHandler, module_name);
        try {
        if (!typeChecker.check(statements)) { errorHandler.printSummary(); m_had_error = true; return nullptr; }
        } catch (const std::exception& e) {
            std::cerr << "\nEXCEPTION in TypeChecker: " << e.what() << "\n";
            m_had_error = true;
            return nullptr;
        }

        auto mod = typeChecker.getModuleType();
        m_angara_module_names.push_back(module_name);

        // --- LLVM Backend ---
        try {
            LLVMBackend llvmBackend(typeChecker, errorHandler, m_target_triple, m_freestanding);
            if (!llvmBackend.generate(statements, mod, m_angara_module_names)) {
                m_had_error = true;
                return nullptr;
            }
            m_generated_object_files.insert(llvmBackend.get_object_file_path());
        } catch (const std::exception& e) {
            std::cerr << "\nEXCEPTION in LLVM backend: " << e.what() << "\n";
            m_had_error = true;
            return nullptr;
        }
        m_modules_compiled++;
        print_progress("Done!");
        std::cout << "\r\033[K" << std::flush;
        return mod;
    }

    std::shared_ptr<ModuleType> CompilerDriver::loadNativeModule(const std::string& path, const Token& import_token) {
        print_progress("Loading native module: " + path);

        void* handle = dlopen(path.c_str(), RTLD_LAZY);
        if (!handle) {
            std::cerr << "\n" << CLR_BOLD << CLR_RED << "Error at line " << import_token.line << CLR_RESET
                      << ": Could not load native module '" << path << "'. Reason: " << dlerror() << "\n";
            m_had_error = true;
            return nullptr;
        }

        std::string module_name = get_base_name(path);
        std::string init_func_name = "Angara_" + module_name + "_Init";

        // The init function receives a vtable of runtime functions.
        // At compile-time, we pass NULL — the module should only define
        // its export table during init, not call runtime functions.
        typedef const AngaraFuncDef* (*AngaraModuleInitFn)(int*, const AngaraAPI*);
        auto init_fn = (AngaraModuleInitFn)dlsym(handle, init_func_name.c_str());

        if (!init_fn) {
            std::cerr << "\n" << CLR_BOLD << CLR_RED << "Error at line " << import_token.line << CLR_RESET
                      << ": Invalid native module '" << path << "'. Missing entry point: " << init_func_name << "\n";
            m_had_error = true;
            dlclose(handle);
            return nullptr;
        }

        int def_count = 0;
        const AngaraFuncDef* defs = init_fn(&def_count, nullptr);

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

                        // --- THIS IS THE FIX ---
                        // A variadic marker must be the last thing in the parameter list.
                        // This means the very next character MUST be the '->' arrow.
                        if (parser.peek() != '-') {
                            throw std::runtime_error("Variadic '...' must be the final item in the parameter list before '->'.");
                        }
                        // The `break` is also essential, as it stops the loop from trying to parse more parameters.
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
                        for (int m = 0; class_def->methods[m].name != nullptr; ++m) {
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
                        for (int f = 0; class_def->fields[f].name != nullptr; ++f) {
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
                std::cerr << "\n" << CLR_BOLD << CLR_YELLOW << "Warning:" << CLR_RESET << " Could not parse ABI definition for '"
                          << (func_def.name ? func_def.name : "unknown")
                          << "' in module '" << path << "': " << e.what() << "\n";
            }
        }

        m_modules_compiled++;
        return module_type;
    }

} // namespace angara