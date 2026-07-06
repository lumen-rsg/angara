#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <chrono>
#include <mutex>
#include <atomic>

#include "ConfigParser.h"
#include "SymbolTable.h"
#include "Token.h"
#include "Type.h"
#include "DependencyGraph.h"

namespace angara {

    // Forward declarations.
    struct Stmt;

    /// Represents a compiled or loaded module and its public exports.
    struct ModuleType : Type {
        const std::string name;
        std::map<std::string, std::shared_ptr<Type>> exports;
        bool is_native = false;

        explicit ModuleType(std::string name)
            : Type(TypeKind::MODULE), name(std::move(name)) {}

        std::string toString() const override { return "module<" + name + ">"; }
    };

    /// TOOL-2: records a module discovered during the discovery phase
    /// (parse-only, before type-checking).  Holds the parsed AST and the
    /// set of import paths extracted from AttachStmt nodes.
    struct ModuleDiscovery {
        std::string path;          // absolute source-file path
        std::string name;          // module name
        std::vector<std::shared_ptr<Stmt>> statements;  // parsed AST
        std::vector<std::string> imports;  // resolved absolute import paths
        bool is_native = false;    // true for .so/.dylib native modules
    };

    /// Top-level driver that orchestrates the full compilation pipeline:
    /// module resolution, lexing, parsing, type checking, and LLVM code generation.
    class CompilerDriver {
    public:
        CompilerDriver();
        virtual ~CompilerDriver();

        /// Sets the search paths for standard library sources and native module binaries.
        /// @param std_lib_path     Path to Angara source modules (e.g. /opt/angara/src/modules).
        /// @param native_lib_path  Path to compiled shared libraries (e.g. /opt/angara/modules).
        void set_paths(std::string std_lib_path, std::string native_lib_path);

        /// Sets the LLVM target triple for cross-compilation.
        void set_target(const std::string& triple) { m_target_triple = triple; }
        const std::string& get_target() const { return m_target_triple; }

        /// Sets the sysroot path for the cross-compilation linker.
        void set_sysroot(const std::string& path) { m_sysroot = path; }
        const std::string& get_sysroot() const { return m_sysroot; }

        /// Enables or disables freestanding mode (no libc, bare-metal).
        void set_freestanding(bool val) { m_freestanding = val; }
        bool is_freestanding() const { return m_freestanding; }

        /// Enables or disables nostdlib mode (skip linking standard libraries).
        void set_nostdlib(bool val) { m_nostdlib = val; }
        bool is_nostdlib() const { return m_nostdlib; }

        /// Enables or disables check-only mode (lex + parse + typecheck, skip LLVM codegen).
        void set_check_only(bool val) { m_check_only = val; }
        bool is_check_only() const { return m_check_only; }

        /// Enables or disables quiet mode (suppress all stdout output).
        void set_quiet(bool val) { m_quiet = val; }
        bool is_quiet() const { return m_quiet; }

        /// Enables or disables IR dump (emit unoptimized .ll files).
        void set_dump_ir(bool val) { m_dump_ir = val; }
        bool is_dump_ir() const { return m_dump_ir; }

        /// Enables emitting LLVM IR to stdout instead of compiling.
        void set_emit_llvm(bool val) { m_emit_llvm = val; }
        bool is_emit_llvm() const { return m_emit_llvm; }

        /// Enables or disables LTO (Link-Time Optimization).
        void set_lto(bool val) { m_lto = val; }
        bool is_lto() const { return m_lto; }

        /// Enables or disables debug mode (O0 optimization + DWARF debug info).
        void set_debug(bool val) { m_debug = val; }
        bool is_debug() const { return m_debug; }

        /// Sets the DWARF version for debug info (0 = auto-detect by platform).
        void set_dwarf_version(int v) { m_dwarf_version = v; }
        int get_dwarf_version() const { return m_dwarf_version; }

        /// Configures warning control flags.
        void set_warnings_as_errors(bool val) { m_werror = val; }
        void set_wall(bool val) { m_wall = val; }
        void suppress_warning(const std::string& code) { m_suppressed_warnings.insert(code); }

        /// Sets the diagnostic output format.
        void set_error_format(const std::string& fmt) { m_error_format = fmt; }

        /// Sets the GC strategy ("chaperone" or "mark-sweep").

        /// Compiles a root source file and all its transitive imports.
        /// Runs the full pipeline: discover → compile modules in dependency order.
        /// This is the main entry point for the build system.
        /// @param project         The project configuration.
        /// @param root_file_path  Absolute path to the entry source file.
        /// @return True if all stages succeeded.
        bool compile(const ProjectConfig& project, const std::string& root_file_path);

        /// TOOL-2: Phase 1 — discovers all reachable modules by parsing them
        /// (no type-checking) and builds the dependency graph.  Native modules
        /// are loaded immediately.
        /// @param root_file_path  Absolute path to the entry source file.
        /// @return True if discovery succeeded with no errors.
        bool discoverModules(const std::string& root_file_path);

        /// TOOL-2: Phase 2 — compiles all discovered modules in topological
        /// order, using the thread pool for parallelism within each level.
        /// Must be called after a successful discoverModules().
        /// @return True if all modules compiled successfully.
        bool compileDiscoveredModules();

        /// TOOL-2: compiles a single module through type-check → chaperone →
        /// LLVM codegen, reusing the AST saved during discovery.  Called from
        /// worker threads during parallel compilation.
        /// @param path         Absolute module path.
        /// @param is_clean     If true (incremental cache hit), skip LLVM codegen
        ///                     and reuse the cached .o file.
        /// @return True if compilation succeeded.
        bool compileOneModule(const std::string& path, bool is_clean);

        /// Resolves a module by path or identifier, searching the configured paths.
        /// Returns a cached module if already compiled. Triggers recursive compilation
        /// for Angara sources, or native loading for shared libraries.
        /// @param path_or_id    The module path, name, or project identifier.
        /// @param import_token  The 'attach' token (for error location), or empty.
        /// @return The resolved module, or nullptr on failure.
        std::shared_ptr<ModuleType> resolveModule(
            const std::string& path_or_id,
            const Token& import_token
        );

        /// Compiles a single Angara source file through all pipeline stages.
        /// @param path          Absolute path to the .an source file.
        /// @param module_name   A unique name for this compilation unit.
        /// @return The resulting module type, or nullptr on failure.
        std::shared_ptr<ModuleType> compileAngaraSource(
            const std::string& path,
            const std::string& module_name
        );

        /// Returns the set of generated object file paths from LLVM codegen.
        const std::set<std::string>& get_generated_object_files() const;

        /// Returns the list of native library names that were linked during compilation.
        const std::vector<std::string>& get_native_libs_linked() const;

        /// Extracts the base filename without extension, stripping any "lib" prefix.
        /// @param path  A file path.
        /// @return The base name (e.g. "io" from "/opt/angara/modules/libio.so").
        static std::string get_base_name(const std::string& path);

        /// Reads a file's contents into a string. Returns empty string on failure.
        static std::string read_file(const std::string& path);

        /// Sets the map of project names to their absolute entry file paths.
        inline void set_workspace_projects(std::map<std::string, std::string> project_entries) {
            m_project_entries = std::move(project_entries);
        }

        /// TOOL-2: sets the number of parallel compilation threads.
        /// 0 = use std::thread::hardware_concurrency().
        inline void set_jobs(int n) { m_jobs = n; }
        inline int get_jobs() const { return m_jobs; }

        /// TOOL-2: forces a full rebuild, ignoring the incremental cache.
        inline void set_force_rebuild(bool v) { m_force_rebuild = v; }
        inline bool get_force_rebuild() const { return m_force_rebuild; }

        /// TOOL-2: sets the build output directory (for .o files and manifest).
        inline void set_build_dir(const std::string& dir) { m_build_dir = dir; }
        inline const std::string& get_build_dir() const { return m_build_dir; }

        /// TOOL-1: adds a directory to search for .an source packages.
        /// These paths are searched BEFORE the standard library path,
        /// allowing packages to shadow/override stdlib modules.
        inline void add_package_search_path(const std::string& path) {
            m_package_search_paths.push_back(path);
        }

    protected:
        /// Prints a log line, temporarily clearing the progress bar.
        void log_step(const std::string& message);

        /// Loads a native shared library and registers its exported functions and classes.
        /// @param path          Absolute path to the .so/.dylib/.dll.
        /// @param import_token  The 'attach' token for error reporting.
        /// @return The loaded module, or nullptr on failure.
        std::shared_ptr<ModuleType> loadNativeModule(const std::string& path, const Token& import_token);

        bool m_had_error = false;

        std::string m_angara_module_path;
        std::string m_native_module_path;

        // TOOL-1: additional search paths for .an packages.
        std::vector<std::string> m_package_search_paths;

        std::map<std::string, std::shared_ptr<ModuleType>> m_module_cache;
        std::vector<std::string> m_compilation_stack;
        SymbolTable m_global_symbols;
        std::map<std::string, std::string> m_project_entries;

        std::string m_target_triple;
        std::string m_sysroot;

        bool m_freestanding = false;
        bool m_nostdlib = false;
        bool m_check_only = false;
        bool m_quiet = false;
        bool m_dump_ir = false;
        bool m_emit_llvm = false;
        bool m_lto = false;
        bool m_debug = false;
        int m_dwarf_version = 0;   // 0 = auto-detect by platform
        bool m_werror = false;
        bool m_wall = false;
        std::set<std::string> m_suppressed_warnings;
        std::string m_error_format = "text";

        std::set<std::string> m_generated_object_files;
        std::vector<std::string> m_angara_module_names;
        std::vector<std::string> m_native_lib_names;

        int m_total_modules = 0;
        std::atomic<int> m_modules_compiled{0};
        std::string m_last_progress_message;
        std::chrono::time_point<std::chrono::high_resolution_clock> m_build_start_time;

        // TOOL-2: incremental + parallel compilation state
        bool m_force_rebuild = false;
        int m_jobs = 0;  // 0 = use hardware_concurrency
        std::string m_build_dir;
        std::map<std::string, ModuleDiscovery> m_discovered_modules;
        DependencyGraph m_dependency_graph;

        // TOOL-2: thread-safety for parallel compilation
        mutable std::mutex m_cache_mutex;     // protects m_module_cache writes
        mutable std::mutex m_obj_files_mutex; // protects m_generated_object_files

        /// TOOL-2: resolves an import path to an absolute file path without
        /// triggering compilation.  Uses the same search order as resolveModule().
        std::string resolveImportPath(const std::string& path_or_id,
                                      const Token& import_token);

        /// Open dlopen handles for native modules — closed in destructor.
        std::vector<void*> m_native_handles;

        /// Renders the compilation progress bar to stdout.
        void print_progress(const std::string& current_file);
    };

}
