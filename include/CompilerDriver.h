#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <SymbolTable.h>
#include <Token.h>
#include <set>

#include "Type.h" // For our internal Type representation

namespace angara {

    // --- NEW: ModuleType to represent the public API of a module ---
    struct ModuleType : Type {
        const std::string name;
        // A map from exported symbol name to its Type.
        std::map<std::string, std::shared_ptr<Type>> exports;
        bool is_native = false;

        explicit ModuleType(std::string name)
            : Type(TypeKind::MODULE), name(std::move(name)) {}

        std::string toString() const override { return "module<" + name + ">"; }
    };


    class CompilerDriver {
    public:
        CompilerDriver();
        bool compile(const std::string& root_file_path);
        std::shared_ptr<ModuleType> resolveModule(const std::string& path_or_id, const Token& import_token);
        static std::string get_base_name(const std::string& path);


    private:
        std::string read_file(const std::string& path);

        // --- NEWLY DECLARED MEMBERS ---
        void log_step(const std::string& message);
        void print_progress(const std::string& current_file);
        bool m_had_error = false;
        // --- END OF NEW MEMBERS ---

        std::map<std::string, std::shared_ptr<ModuleType>> m_module_cache;
        std::vector<std::string> m_compilation_stack;

        // --- NEW: Track files for the final link step ---
        std::vector<std::string> m_compiled_c_files;
        // --- END OF NEW ---

        int m_total_modules = 0;
        int m_modules_compiled = 0;
        std::vector<std::string> m_compiled_module_names;
        std::shared_ptr<ModuleType> loadNativeModule(const std::string& path, const Token& import_token);
        SymbolTable m_global_symbols;
        // --- THIS IS THE FIX ---
        // Use a set to automatically store only unique library directories.
        std::set<std::string> m_native_lib_paths;
        // Store the clean library names (e.g., "fs", "http").
        std::vector<std::string> m_native_lib_names;
        // --- END OF FIX ---

        const std::string m_runtime_path;
        const std::string m_angara_module_path;
        const std::string m_native_module_path;

    };

} // namespace angara