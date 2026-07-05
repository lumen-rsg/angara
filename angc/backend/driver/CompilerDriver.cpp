#include "Chaperone.h"
#include "CompilerDriver.h"
#include "ErrorHandler.h"
#include "Lexer.h"
#include "Parser.h"
#include "TypeChecker.h"
#include "LLVMBackend.h"
#include "AngaraABI.h"
#include "Colors.h"
#include "ThreadPool.h"
#include "BuildManifest.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <future>

#include <dlfcn.h>
#include <filesystem>

namespace angara {

    class TypeStringParser {
    public:
        TypeStringParser(const std::string& str,
                         std::map<std::string, std::shared_ptr<ClassType>>& known_classes)
                : m_source(str), m_known_classes(known_classes) {}

        std::shared_ptr<Type> parse_single_type() {
            return parse_optional();
        }

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
        std::shared_ptr<Type> parse_optional() {
            auto base_type = parse_base();
            if (!is_at_end() && peek() == '?') {
                consume('?');
                return std::make_shared<OptionalType>(base_type);
            }
            return base_type;
        }

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

            m_current++;
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
        if (m_quiet) return;
        std::cout << "\r\033[K";
        std::cout << CLR_BOLD << CLR_GREEN << "-> " << CLR_RESET << CLR_BOLD << message << CLR_RESET << std::endl;
        print_progress(m_last_progress_message);
    }

    CompilerDriver::CompilerDriver() {
        m_angara_module_path = ".";
        m_native_module_path = ".";
    }

    CompilerDriver::~CompilerDriver() {
        for (void* handle : m_native_handles) {
            if (handle) dlclose(handle);
        }
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
        if (m_quiet) return;
        m_last_progress_message = current_file;

        int bar_width = 20;
        float progress = (m_total_modules > 0) ? (float)m_modules_compiled / m_total_modules : 0;
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
        m_discovered_modules.clear();
        m_dependency_graph = DependencyGraph();  // reset

        // TOOL-2 Phase 1: discover all modules (parse only, no type-checking)
        if (!discoverModules(root_file_path)) {
            return false;
        }

        // TOOL-2 Phase 2: compile in topological order (parallel within each level)
        if (!compileDiscoveredModules()) {
            return false;
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - m_build_start_time);
        double seconds = duration.count() / 1000.0;
        int total_modules = static_cast<int>(m_generated_object_files.size());
        int native_count = static_cast<int>(m_native_lib_names.size());

        if (!m_quiet) {
            std::cout << CLR_BOLD << CLR_GREEN << "[OK] " << CLR_RESET << "Compiled "
                      << total_modules << " module" << (total_modules != 1 ? "s" : "")
                      << (native_count > 0 ? " + " + std::to_string(native_count) + " native lib" + (native_count != 1 ? "s" : "") : "")
                      << " in " << CLR_BOLD << seconds << "s" << CLR_RESET << std::endl;
        }

        return true;
    }

    // ── TOOL-2: resolveImportPath ──────────────────────────────────────────
    // Extracts the path-resolution logic from resolveModule() without triggering
    // compilation.  Returns the absolute file path on success, or empty string
    // if the module cannot be found.

    // Forward-declared: defined later in this file (used by resolveModule too).
    static std::optional<std::string> find_candidate(const std::filesystem::path& dir,
                                                      const std::string& name);

    std::string CompilerDriver::resolveImportPath(const std::string& path_or_id,
                                                   const Token& import_token) {
        namespace fs = std::filesystem;
        std::string found_path;

        fs::path base_dir = fs::current_path();
        if (import_token.file && !import_token.file->empty()) {
            base_dir = fs::path(*import_token.file).parent_path();
        }

        fs::path input_path(path_or_id);
        if (input_path.is_absolute()) {
            std::error_code ec;
            if (fs::exists(input_path, ec))
                found_path = fs::canonical(input_path, ec).string();
        }

        if (found_path.empty()) {
            bool is_relative = (path_or_id.find("./") == 0 ||
                                path_or_id.find("../") == 0);

            if (is_relative) {
                if (auto p = find_candidate(base_dir, path_or_id))
                    found_path = *p;
            } else {
                if (m_project_entries.count(path_or_id))
                    found_path = m_project_entries.at(path_or_id);
                if (found_path.empty()) {
                    if (auto p = find_candidate(base_dir, path_or_id))
                        found_path = *p;
                }
                if (found_path.empty()) {
                    for (auto const& [name, entry_file] : m_project_entries) {
                        fs::path proj_dir = fs::path(entry_file).parent_path();
                        if (auto p = find_candidate(proj_dir, path_or_id)) {
                            found_path = *p;
                            break;
                        }
                    }
                }
                if (found_path.empty()) {
                    if (auto p = find_candidate(fs::path(m_angara_module_path), path_or_id))
                        found_path = *p;
                }
                if (found_path.empty()) {
                    if (auto p = find_candidate(fs::path(m_native_module_path), path_or_id))
                        found_path = *p;
                }
            }
        }

        if (found_path.empty()) {
            std::string loc = (import_token.file)
                ? *import_token.file : "entry point";
            std::cerr << CLR_RED << "[ERROR] Module '" << path_or_id
                      << "' not found.\n"
                      << "         Searched: project entries, local directory, "
                         "standard library, native modules.\n"
                      << "         Imported from " << loc << CLR_RESET << "\n";
            m_had_error = true;
        }

        return found_path;
    }

    // ── TOOL-2: discoverModules ────────────────────────────────────────────
    // Phase 1: recursively parse every reachable .an module to build the
    // dependency graph.  Native modules are loaded immediately (they have
    // no source to parse and no further imports to discover).
    //
    // This is a recursive method — it calls itself for each discovered import.

    bool CompilerDriver::discoverModules(const std::string& root_file_path) {
        namespace fs = std::filesystem;

        // Already discovered this module?  Skip.
        if (m_discovered_modules.count(root_file_path)) return true;

        // Create a dummy token for the root file (it has no import site).
        Token dummy_token;

        // ── Native module (.so / .dylib / .dll) ─────────────────────
        if (root_file_path.ends_with(".so") ||
            root_file_path.ends_with(".dylib") ||
            root_file_path.ends_with(".dll")) {

            // Load the native module immediately — it has no source to parse
            // and no further imports to discover.
            auto mod = loadNativeModule(root_file_path, dummy_token);
            if (!mod) return false;

            ModuleDiscovery disc;
            disc.path = root_file_path;
            disc.name = mod->name;
            disc.is_native = true;
            m_discovered_modules[root_file_path] = std::move(disc);

            // Register in the dependency graph (leaf node — no imports).
            m_dependency_graph.addModule(root_file_path, mod->name, {});
            return true;
        }

        // ── Angara source (.an) ─────────────────────────────────────
        std::string source = read_file(root_file_path);
        if (source.empty() && !fs::exists(fs::path(root_file_path))) {
            std::cerr << CLR_RED << "[ERROR] Cannot read source file '"
                      << root_file_path << "'." << CLR_RESET << "\n";
            m_had_error = true;
            return false;
        }

        auto filename_ptr = std::make_shared<std::string>(root_file_path);

        ErrorHandler errorHandler(source);
        errorHandler.set_warnings_as_errors(m_werror);
        errorHandler.set_error_format(m_error_format);
        for (const auto& code : m_suppressed_warnings) {
            errorHandler.suppress_warning(code);
        }

        // Lex + Parse only (no type-checking).
        Lexer lexer(source, filename_ptr, errorHandler);
        auto tokens = lexer.scanTokens();
        if (errorHandler.hadError()) {
            errorHandler.printSummary();
            m_had_error = true;
            return false;
        }

        Parser parser(tokens, errorHandler);
        auto statements = parser.parseStmts();
        if (errorHandler.hadError()) {
            errorHandler.printSummary();
            m_had_error = true;
            return false;
        }

        // Determine module name (same logic as resolveModule).
        std::string module_name;
        for (auto const& [projName, entryPath] : m_project_entries) {
            if (root_file_path == entryPath) {
                module_name = projName;
                break;
            }
        }
        if (module_name.empty()) {
            for (auto const& [projName, entryPath] : m_project_entries) {
                fs::path proj_dir = fs::path(entryPath).parent_path();
                if (root_file_path.find(proj_dir.string()) == 0) {
                    module_name = projName + "_" + get_base_name(root_file_path);
                    break;
                }
            }
        }
        if (module_name.empty()) {
            module_name = get_base_name(root_file_path);
        }
        if (module_name == "main") {
            module_name = "app_main";
        }

        // Extract import paths from AttachStmt nodes.
        std::vector<std::string> import_paths;
        for (const auto& stmt : statements) {
            auto attach = std::dynamic_pointer_cast<const AttachStmt>(stmt);
            if (!attach) continue;

            // attach.modulePath.lexeme is the unresolved import path string,
            // e.g. "./mymodule" or "io".
            std::string resolved = resolveImportPath(
                attach->modulePath.lexeme, attach->modulePath);
            if (resolved.empty()) {
                // resolveImportPath already set m_had_error and printed.
                return false;
            }
            import_paths.push_back(resolved);
        }

        // Store the discovery record.
        ModuleDiscovery disc;
        disc.path = root_file_path;
        disc.name = module_name;
        disc.statements = std::move(statements);
        disc.imports = import_paths;
        m_discovered_modules[root_file_path] = disc;

        // Register in the dependency graph.
        m_dependency_graph.addModule(root_file_path, module_name, import_paths);

        // Recursively discover imports.
        // Note: import_paths is a copy since disc was moved.
        for (const auto& import_path : m_discovered_modules[root_file_path].imports) {
            if (!discoverModules(import_path)) {
                return false;
            }
        }

        return true;
    }

    // ── TOOL-2: compileDiscoveredModules ───────────────────────────────────
    // Phase 2: compiles modules in topological order with incremental caching.
    // - Loads the build manifest to determine which modules are clean.
    // - Propagates dirtiness: if A imports B and B changed, A is dirty too.
    // - Clean modules skip LLVM codegen and reuse cached .o files.
    // - Modules at the same topological level are compiled in parallel.

    // Helper: returns a numeric mtime for a file path (for equality comparison).
    static int64_t file_mtime(const std::string& path) {
        std::error_code ec;
        auto ftime = std::filesystem::last_write_time(path, ec);
        if (ec) return 0;
        return ftime.time_since_epoch().count();
    }

    bool CompilerDriver::compileDiscoveredModules() {
        namespace fs = std::filesystem;

        auto levels = m_dependency_graph.topologicalLevels();

        if (levels.empty() && m_dependency_graph.size() > 0) {
            std::cerr << CLR_RED << "[ERROR] Circular dependency detected in "
                      << "module graph." << CLR_RESET << "\n";
            m_had_error = true;
            return false;
        }

        m_total_modules = static_cast<int>(m_dependency_graph.size());

        // ── Load manifest ────────────────────────────────────────────────
        BuildManifest manifest;
        if (!m_force_rebuild && !m_build_dir.empty()) {
            std::string manifest_path = m_build_dir + "/manifest.json";
            manifest.load(manifest_path);
        }

        // ── Compute dirtiness ────────────────────────────────────────────
        // A module is "clean" if its source mtime and all dependency mtimes
        // match the manifest AND the --force flag is not set.
        // If a module is dirty, all modules that transitively import it are
        // also dirty (their type-checking depends on the changed exports).
        std::set<std::string> dirty_modules;

        for (const auto& [path, disc] : m_discovered_modules) {
            if (disc.is_native) continue;  // native modules never need recompilation

            if (m_force_rebuild) {
                dirty_modules.insert(path);
                continue;
            }

            // Gather current mtimes.
            int64_t src_mtime = file_mtime(path);
            std::map<std::string, int64_t> dep_mtimes;
            for (const auto& dep : disc.imports) {
                // For native modules, use 0 as mtime (they don't change).
                auto dit = m_discovered_modules.find(dep);
                if (dit != m_discovered_modules.end() && dit->second.is_native) {
                    dep_mtimes[dep] = 0;
                } else {
                    dep_mtimes[dep] = file_mtime(dep);
                }
            }

            if (!manifest.isClean(path, src_mtime, dep_mtimes)) {
                dirty_modules.insert(path);
            }
        }

        // Propagate dirtiness transitively.
        // If A imports B and B is dirty, A is dirty too.
        std::set<std::string> all_dirty = dirty_modules;  // copy
        bool changed = true;
        while (changed) {
            changed = false;
            for (const auto& [path, disc] : m_discovered_modules) {
                if (all_dirty.count(path)) continue;  // already dirty
                for (const auto& dep : disc.imports) {
                    if (all_dirty.count(dep)) {
                        all_dirty.insert(path);
                        changed = true;
                        break;
                    }
                }
            }
        }

        if (!m_quiet && !all_dirty.empty()) {
            int clean_count = static_cast<int>(m_discovered_modules.size()) -
                              static_cast<int>(all_dirty.size());
            std::cout << CLR_DIM << "   Incremental: " << all_dirty.size()
                      << " dirty, " << clean_count << " clean"
                      << CLR_RESET << "\n";
        }

        // ── Compile ──────────────────────────────────────────────────────
        int num_threads = m_jobs;
        if (num_threads <= 0) {
            num_threads = static_cast<int>(
                std::thread::hardware_concurrency());
            if (num_threads <= 0) num_threads = 1;
        }

        ThreadPool pool(static_cast<size_t>(num_threads));

        for (const auto& level : levels) {
            if (m_had_error) break;

            std::vector<std::future<bool>> futures;
            futures.reserve(level.size());

            for (const auto& path : level) {
                if (m_had_error) break;

                auto& disc = m_discovered_modules[path];
                if (disc.is_native) {
                    m_modules_compiled++;
                    continue;
                }

                bool is_clean = !all_dirty.count(path);

                // For clean modules, add the cached .o file to the link set
                // before dispatching (compileOneModule skips codegen for these).
                if (is_clean) {
                    std::string obj_file = m_build_dir.empty()
                        ? "ang_" + disc.name + ".o"
                        : m_build_dir + "/ang_" + disc.name + ".o";
                    {
                        std::lock_guard<std::mutex> lock(m_obj_files_mutex);
                        m_generated_object_files.insert(obj_file);
                    }
                }

                futures.push_back(pool.enqueue([this, path, is_clean]() -> bool {
                    return compileOneModule(path, is_clean);
                }));
            }

            // Barrier: wait for all modules in this level.
            bool level_ok = true;
            for (auto& f : futures) {
                bool ok = f.get();
                if (!ok) level_ok = false;
            }

            if (!level_ok) break;
        }

        // ── Update manifest ──────────────────────────────────────────────
        // Record freshly-compiled (dirty) modules so they are clean next time.
        if (!m_had_error && !m_build_dir.empty()) {
            for (const auto& path : all_dirty) {
                auto dit = m_discovered_modules.find(path);
                if (dit == m_discovered_modules.end()) continue;
                const auto& disc = dit->second;

                int64_t src_mtime = file_mtime(path);
                std::map<std::string, int64_t> dep_mtimes;
                for (const auto& dep : disc.imports) {
                    dep_mtimes[dep] = file_mtime(dep);
                }

                // Determine the .o file path.
                std::string obj_file = m_build_dir + "/ang_" + disc.name + ".o";

                manifest.addEntry(path, disc.name, src_mtime,
                                  dep_mtimes, obj_file);
            }
            manifest.save();
        }

        return !m_had_error;
    }

    // ── TOOL-2: compileOneModule ───────────────────────────────────────────
    // Compiles a single module through type-check → chaperone → LLVM codegen.
    // Called from worker threads during parallel compilation — all shared
    // state access is mutex-protected.

    bool CompilerDriver::compileOneModule(const std::string& path, bool is_clean) {
        auto it = m_discovered_modules.find(path);
        if (it == m_discovered_modules.end()) {
            std::cerr << CLR_RED << "[ERROR] Module '" << path
                      << "' was not discovered." << CLR_RESET << "\n";
            m_had_error = true;
            return false;
        }

        const auto& disc = it->second;

        // Each worker creates its own pipeline objects — no shared mutable
        // state across threads except what is explicitly synchronized below.
        std::string source = read_file(path);
        auto filename_ptr = std::make_shared<std::string>(path);

        // Thread-local error handler.
        ErrorHandler errorHandler(source);
        errorHandler.set_warnings_as_errors(m_werror);
        errorHandler.set_error_format(m_error_format);
        for (const auto& code : m_suppressed_warnings) {
            errorHandler.suppress_warning(code);
        }

        // Lex + Parse (fast, re-done per module for fresh ownership).
        Lexer lexer(source, filename_ptr, errorHandler);
        auto tokens = lexer.scanTokens();
        if (errorHandler.hadError()) {
            std::lock_guard<std::mutex> lock(m_cache_mutex);
            errorHandler.printSummary();
            m_had_error = true;
            return false;
        }

        Parser parser(tokens, errorHandler);
        auto statements = parser.parseStmts();
        if (errorHandler.hadError()) {
            std::lock_guard<std::mutex> lock(m_cache_mutex);
            errorHandler.printSummary();
            m_had_error = true;
            return false;
        }

        // Type-checking.  `m_module_cache` is read-only for already-compiled
        // dependencies (safe without lock since shared_ptr is thread-safe for
        // reads).  New cache insertions are locked below.
        TypeChecker typeChecker(*this, errorHandler, disc.name);
        try {
            if (!typeChecker.check(statements)) {
                std::lock_guard<std::mutex> lock(m_cache_mutex);
                errorHandler.printSummary();
                m_had_error = true;
                return false;
            }
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(m_cache_mutex);
            std::cerr << "\n" << CLR_RED
                      << "[ERROR] Type checker threw an exception while "
                         "processing '" << path << "'.\n"
                      << "         " << e.what() << CLR_RESET << "\n";
            m_had_error = true;
            return false;
        }

        auto mod = typeChecker.getModuleType();

        // Check-only mode: stop after type-checking.
        if (m_check_only) {
            m_modules_compiled++;
            {
                std::lock_guard<std::mutex> lock(m_cache_mutex);
                m_module_cache[path] = mod;
            }
            return true;
        }

        // Chaperone pass (v5).
        try {
            Chaperone::run(statements, typeChecker, errorHandler);
            if (errorHandler.hadError()) {
                std::lock_guard<std::mutex> lock(m_cache_mutex);
                errorHandler.printSummary();
                m_had_error = true;
                return false;
            }
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(m_cache_mutex);
            std::cerr << "\n" << CLR_RED
                      << "[ERROR] Chaperone threw an exception while "
                         "processing '" << path << "'.\n"
                      << "         " << e.what() << CLR_RESET << "\n";
            m_had_error = true;
            return false;
        }

        // LLVM codegen (skip if incremental cache hit).
        if (is_clean) {
            m_modules_compiled++;
            {
                std::lock_guard<std::mutex> lock(m_cache_mutex);
                m_module_cache[path] = mod;
            }
            return true;
        }

        try {
            LLVMBackend llvmBackend(typeChecker, errorHandler, m_target_triple,
                                     m_freestanding, m_dump_ir, m_debug,
                                     m_emit_llvm);
            if (!m_build_dir.empty()) {
                llvmBackend.set_output_dir(m_build_dir);
            }
            if (!llvmBackend.generate(statements, mod, m_angara_module_names)) {
                std::lock_guard<std::mutex> lock(m_cache_mutex);
                m_had_error = true;
                return false;
            }
            {
                std::lock_guard<std::mutex> lock(m_obj_files_mutex);
                m_generated_object_files.insert(
                    llvmBackend.get_object_file_path());
            }
            {
                std::lock_guard<std::mutex> lock(m_cache_mutex);
                m_angara_module_names.push_back(disc.name);
            }
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(m_cache_mutex);
            std::cerr << "\n" << CLR_RED
                      << "[ERROR] LLVM backend threw an exception while "
                         "generating code for '" << path << "'.\n"
                      << "         " << e.what() << CLR_RESET << "\n";
            m_had_error = true;
            return false;
        }

        m_modules_compiled++;
        {
            std::lock_guard<std::mutex> lock(m_cache_mutex);
            m_module_cache[path] = mod;
        }
        return true;
    }

    std::string CompilerDriver::get_base_name(const std::string& path) {
        size_t last_slash = path.find_last_of("/\\");
        size_t start = (last_slash == std::string::npos) ? 0 : last_slash + 1;

        size_t last_dot = path.find_last_of('.');
        if (last_dot == std::string::npos || last_dot < start) {
            last_dot = path.length();
        }

        std::string basename = path.substr(start, last_dot - start);
        if (basename.rfind("lib", 0) == 0) {
            return basename.substr(3);
        }
        return basename;
    }

    static std::optional<std::string> find_candidate(const std::filesystem::path& dir, const std::string& name) {
        std::error_code ec;

        if (std::filesystem::path(name).has_extension()) {
            std::filesystem::path p = dir / name;
            if (std::filesystem::exists(p, ec) && !std::filesystem::is_directory(p, ec)) {
                return std::filesystem::canonical(p).string();
            }
        }

        std::filesystem::path an_path = dir / (name + ".an");
        if (std::filesystem::exists(an_path, ec) && !std::filesystem::is_directory(an_path, ec)) {
            return std::filesystem::canonical(an_path).string();
        }

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
    std::string module_name = "";

    fs::path base_dir = fs::current_path();
    if (import_token.file && !import_token.file->empty()) {
        base_dir = fs::path(*import_token.file).parent_path();
    }

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
            if (m_project_entries.count(path_or_id)) {
                found_path = m_project_entries.at(path_or_id);
            }
            if (found_path.empty()) {
                if (auto p = find_candidate(base_dir, path_or_id)) found_path = *p;
            }
            if (found_path.empty()) {
                for (auto const& [name, entry_file] : m_project_entries) {
                    fs::path proj_dir = fs::path(entry_file).parent_path();
                    if (auto p = find_candidate(proj_dir, path_or_id)) {
                        found_path = *p;
                        break;
                    }
                }
            }
            if (found_path.empty()) {
                if (auto p = find_candidate(fs::path(m_angara_module_path), path_or_id)) found_path = *p;
            }
            if (found_path.empty()) {
                if (auto p = find_candidate(fs::path(m_native_module_path), path_or_id)) found_path = *p;
            }
        }
    }

    if (found_path.empty()) {
        std::string loc = (import_token.file) ? *import_token.file : "entry point";
        std::cerr << CLR_RED << "[ERROR] Module '" << path_or_id << "' not found.\n"
                  << "         Searched: project entries, local directory, standard library, native modules.\n"
                  << "         Imported from " << loc << CLR_RESET << "\n";
        m_had_error = true;
        return nullptr;
    }

    for (auto const& [projName, entryPath] : m_project_entries) {
        if (found_path == entryPath) {
            module_name = projName;
            break;
        }
    }

    if (module_name.empty()) {
        for (auto const& [projName, entryPath] : m_project_entries) {
            fs::path proj_dir = fs::path(entryPath).parent_path();
            if (found_path.find(proj_dir.string()) == 0) {
                module_name = projName + "_" + get_base_name(found_path);
                break;
            }
        }
    }

    if (module_name.empty()) {
        module_name = get_base_name(found_path);
    }

    if (module_name == "main") {
        module_name = "app_main";
    }

    if (m_module_cache.count(found_path)) return m_module_cache[found_path];

    for (const auto& s : m_compilation_stack) {
        if (s == found_path) {
            std::cerr << CLR_RED << "[ERROR] Circular dependency detected: '" << found_path << "'.\n"
                      << "         A module cannot import itself, directly or indirectly." << CLR_RESET << "\n";
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
        errorHandler.set_warnings_as_errors(m_werror);
        errorHandler.set_error_format(m_error_format);
        for (const auto& code : m_suppressed_warnings) {
            errorHandler.suppress_warning(code);
        }

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
            std::cerr << "\n" << CLR_RED << "[ERROR] Type checker threw an exception while processing '" << path << "'.\n"
                      << "         " << e.what() << CLR_RESET << "\n";
            m_had_error = true;
            return nullptr;
        }

        auto mod = typeChecker.getModuleType();

        if (m_check_only) {
            m_modules_compiled++;
            if (!m_quiet) {
                print_progress("Done!");
                std::cout << "\r\033[K" << std::flush;
            }
            return mod;
        }

        try {
            // v5: Chaperone pass — compile-time memory verification.
            // Errors (E501–E506) are fatal: do not ship a binary the pass
            // has flagged. Mirrors the type-checker bail at the analogous site.
            Chaperone::run(statements, typeChecker, errorHandler);
            if (errorHandler.hadError()) {
                errorHandler.printSummary();
                m_had_error = true;
                return nullptr;
            }

            LLVMBackend llvmBackend(typeChecker, errorHandler, m_target_triple, m_freestanding, m_dump_ir, m_debug, m_emit_llvm);
            if (!llvmBackend.generate(statements, mod, m_angara_module_names)) {
                m_had_error = true;
                return nullptr;
            }
            m_generated_object_files.insert(llvmBackend.get_object_file_path());
            m_angara_module_names.push_back(module_name);
        } catch (const std::exception& e) {
            std::cerr << "\n" << CLR_RED << "[ERROR] LLVM backend threw an exception while generating code for '" << path << "'.\n"
                      << "         " << e.what() << CLR_RESET << "\n";
            m_had_error = true;
            return nullptr;
        }
        m_modules_compiled++;
        if (!m_quiet) {
            print_progress("Done!");
            std::cout << "\r\033[K" << std::flush;
        }
        return mod;
    }

    std::shared_ptr<ModuleType> CompilerDriver::loadNativeModule(const std::string& path, const Token& import_token) {
        print_progress("Loading native module: " + path);

        void* handle = dlopen(path.c_str(), RTLD_LAZY);
        if (!handle) {
            std::cerr << "\n" << CLR_RED << "[ERROR] Could not load native module '" << path << "'.\n"
                      << "         " << dlerror() << "\n"
                      << "         Ensure the shared library is compatible with your platform." << CLR_RESET << "\n";
            m_had_error = true;
            return nullptr;
        }

        std::string module_name = get_base_name(path);
        std::string init_func_name = "Angara_" + module_name + "_Init";

        typedef const AngaraFuncDef* (*AngaraModuleInitFn)(int*, const AngaraAPI*);
        auto init_fn = (AngaraModuleInitFn)dlsym(handle, init_func_name.c_str());

        if (!init_fn) {
            std::cerr << "\n" << CLR_RED << "[ERROR] Invalid native module '" << path << "'.\n"
                      << "         Missing entry point: '" << init_func_name << "'.\n"
                      << "         The module may not be a valid Angara native extension." << CLR_RESET << "\n";
            m_had_error = true;
            dlclose(handle);
            return nullptr;
        }

        int def_count = 0;
        const AngaraFuncDef* defs = init_fn(&def_count, nullptr);

        auto module_type = std::make_shared<ModuleType>(module_name);
        module_type->is_native = true;

        std::map<std::string, std::shared_ptr<ClassType>> native_classes;

        for (int i = 0; i < def_count; i++) {
            const AngaraFuncDef& func_def = defs[i];
            if (func_def.constructs) {
                const AngaraClassDef* class_def = func_def.constructs;
                // If already registered from a prior constructs entry, skip.
                // But if it was only forward-declared (pre-registered as a
                // placeholder), replace it with a proper ClassType and add
                // to exports.
                auto existing = native_classes.find(class_def->name);
                if (existing != native_classes.end()) {
                    // Check if it was already properly exported
                    if (module_type->exports.count(class_def->name)) continue;
                    // Forward-declared placeholder — replace it
                }

                auto class_type = std::make_shared<ClassType>(class_def->name);
                class_type->is_native = true;
                class_type->home_module = module_type->name;  // TS-4
                native_classes[class_def->name] = class_type;

                module_type->exports[class_def->name] = class_type;

                // Pre-register ALL classes referenced by this constructor's
                // methods (return types / param types). A method like
                // Connection.channel() returning Channel means Channel must
                // be known to the ABI parser before method parsing begins.
                // Use a forward-declaration set (NOT native_classes) so the
                // real registration via func_def.constructs still runs and
                // properly adds the class to both native_classes and exports.
                if (class_def->methods) {
                    for (int m = 0; class_def->methods[m].name != nullptr; ++m) {
                        const AngaraMethodDef& md = class_def->methods[m];
                        if (!md.type_string) continue;
                        // Scan for uppercase identifiers (class names) in the sig.
                        const char* s = md.type_string;
                        while (*s) {
                            if (isupper(*s)) {
                                std::string cn;
                                while (isalnum(*s) || *s == '_') { cn += *s++; }
                                if (!cn.empty() && !native_classes.count(cn)) {
                                    // Forward-declare: a placeholder so the ABI
                                    // parser can resolve the type. The real
                                    // ClassType (with methods) replaces this
                                    // when the _channel/constructs entry is
                                    // processed in the first loop.
                                    auto ct = std::make_shared<ClassType>(cn);
                                    ct->is_native = true;
                                    ct->home_module = module_type->name;  // TS-4
                                    native_classes[cn] = ct;
                                }
                            } else { s++; }
                        }
                    }
                }
            }
        }

        for (int i = 0; i < def_count; i++) {
            const AngaraFuncDef& func_def = defs[i];
            if (!func_def.name || !func_def.type_string) continue;

            try {
                TypeStringParser parser(func_def.type_string, native_classes);

                std::vector<std::shared_ptr<Type>> params;
                bool is_variadic = false;
                while (!parser.is_at_end() && parser.peek() != '-') {
                    params.push_back(parser.parse_single_type());
                    if (!parser.is_at_end() && parser.peek() == '.') {
                        parser.consume_variadic();
                        is_variadic = true;

                        if (parser.peek() != '-') {
                            throw std::runtime_error("Variadic '...' must be the final item in the parameter list before '->'.");
                        }
                        break;
                    }
                }

                parser.consume('-');
                parser.consume('>');

                auto return_type = parser.parse_single_type();

                if (!parser.is_at_end()) {
                    throw std::runtime_error("Unexpected characters after return type in signature '" + std::string(func_def.type_string) + "'.");
                }

                if (func_def.constructs) {
                    const AngaraClassDef* class_def = func_def.constructs;
                    auto class_type = native_classes.at(class_def->name);

                    if (class_def->methods) {
                        for (int m = 0; class_def->methods[m].name != nullptr; ++m) {
                            const AngaraMethodDef& method_def = class_def->methods[m];
                            if (!method_def.name || !method_def.type_string) continue;

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
                // Skip adding function exports for placeholder entries (NULL
                // function pointer). These exist solely to register dependent
                // classes via their `constructs` field — their methods are
                // parsed above, but the entry itself is not callable.
                if (func_def.function) {
                    module_type->exports[func_def.name] = func_type;
                }

            } catch (const std::runtime_error& e) {
                std::cerr << "\n" << CLR_YELLOW << "[WARN] " << CLR_RESET << "Could not parse ABI definition for '"
                          << (func_def.name ? func_def.name : "unknown")
                          << "' in module '" << path << "': " << e.what() << "\n";
            }
        }

        m_modules_compiled++;
        m_native_handles.push_back(handle);
        return module_type;
    }

}
