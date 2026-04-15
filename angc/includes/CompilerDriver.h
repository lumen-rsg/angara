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

    // --- Backend Selection ---
    enum class BackendKind {
        C_TRANSPILER,
        LLVM
    };

    // --- ModuleType Definition ---
    struct ModuleType : Type {
        const std::string name;
        std::map<std::string, std::shared_ptr<Type>> exports;
        bool is_native = false;

        explicit ModuleType(std::string name)
            : Type(TypeKind::MODULE), name(std::move(name)) {}

        std::string toString() const override { return "module<" + name + ">"; }
    };

    class CompilerDriver {
    public:
        CompilerDriver();
        virtual ~CompilerDriver() = default;

        // Configuration
        void set_paths(std::string std_lib_path, std::string native_lib_path);
        void set_backend(BackendKind backend) { m_backend = backend; }
        BackendKind get_backend() const { return m_backend; }

        // Main Entry Point
        // Recursively transpiles 'root_file_path' and all its imports into C files.
        // Returns true if all stages (Lex, Parse, Check, Transpile) succeeded.
        bool compile(const ProjectConfig& project, const std::string& root_file_path);

        // Core Resolution Logic
        std::shared_ptr<ModuleType> resolveModule(
            const std::string& path_or_id,
            const Token& import_token
        );
        std::shared_ptr<ModuleType> compileAngaraSource(
            const std::string& path,
            const std::string& module_name
        );

        // Output Retrieval (Used by BuildSystem to know what to link)
        const std::set<std::string>& get_generated_c_files() const;
        const std::set<std::string>& get_generated_object_files() const;
        const std::vector<std::string>& get_native_libs_linked() const;

        // Static Utility
        static std::string get_base_name(const std::string& path);
        static std::string read_file(const std::string& path);

        inline void set_workspace_projects(std::map<std::string, std::string> project_entries) {
            m_project_entries = std::move(project_entries);
        }

    protected:
        // Internal Helpers
        void log_step(const std::string& message);
        std::shared_ptr<ModuleType> loadNativeModule(const std::string& path, const Token& import_token);

        // State
        bool m_had_error = false;

        // Search Paths (Configured by BuildSystem)
        std::string m_angara_module_path; // /opt/angara/src/modules
        std::string m_native_module_path; // /opt/angara/modules

        // Compilation State
        std::map<std::string, std::shared_ptr<ModuleType>> m_module_cache;
        std::vector<std::string> m_compilation_stack;
        SymbolTable m_global_symbols; // Global symbols across the compilation unit
        std::map<std::string, std::string> m_project_entries;

        // Outputs
        BackendKind m_backend = BackendKind::C_TRANSPILER;
        std::set<std::string> m_generated_c_files;
        std::set<std::string> m_generated_h_files;
        std::set<std::string> m_generated_object_files;
        std::vector<std::string> m_angara_module_names; // Names for init_globals
        std::vector<std::string> m_native_lib_names;    // For linker arguments

        // Progress Tracking
        int m_total_modules = 0;
        int m_modules_compiled = 0;
        std::string m_last_progress_message;
        std::chrono::time_point<std::chrono::high_resolution_clock> m_build_start_time;
        void print_progress(const std::string& current_file);
    };

} // namespace angara