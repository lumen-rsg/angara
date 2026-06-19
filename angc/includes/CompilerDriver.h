#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <chrono>

#include "ConfigParser.h"
#include "SymbolTable.h"
#include "Token.h"
#include "Type.h"

namespace angara {

    /// Represents a compiled or loaded module and its public exports.
    struct ModuleType : Type {
        const std::string name;
        std::map<std::string, std::shared_ptr<Type>> exports;
        bool is_native = false;

        explicit ModuleType(std::string name)
            : Type(TypeKind::MODULE), name(std::move(name)) {}

        std::string toString() const override { return "module<" + name + ">"; }
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

        /// Enables or disables debug mode (O0 optimization + DWARF debug info).
        void set_debug(bool val) { m_debug = val; }
        bool is_debug() const { return m_debug; }

        /// Configures warning control flags.
        void set_warnings_as_errors(bool val) { m_werror = val; }
        void set_wall(bool val) { m_wall = val; }
        void suppress_warning(const std::string& code) { m_suppressed_warnings.insert(code); }

        /// Sets the diagnostic output format.
        void set_error_format(const std::string& fmt) { m_error_format = fmt; }

        /// Sets the GC strategy ("chaperone" or "mark-sweep").

        /// Compiles a root source file and all its transitive imports.
        /// Runs Lex -> Parse -> TypeCheck -> LLVM codegen for each module.
        /// @param project         The project configuration.
        /// @param root_file_path  Absolute path to the entry source file.
        /// @return True if all stages succeeded.
        bool compile(const ProjectConfig& project, const std::string& root_file_path);

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
        bool m_debug = false;
        bool m_werror = false;
        bool m_wall = false;
        std::set<std::string> m_suppressed_warnings;
        std::string m_error_format = "text";

        std::set<std::string> m_generated_object_files;
        std::vector<std::string> m_angara_module_names;
        std::vector<std::string> m_native_lib_names;

        int m_total_modules = 0;
        int m_modules_compiled = 0;
        std::string m_last_progress_message;
        std::chrono::time_point<std::chrono::high_resolution_clock> m_build_start_time;

        /// Open dlopen handles for native modules — closed in destructor.
        std::vector<void*> m_native_handles;

        /// Renders the compilation progress bar to stdout.
        void print_progress(const std::string& current_file);
    };

}
