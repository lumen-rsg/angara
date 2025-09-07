#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <Token.h>

#include "Type.h" // For our internal Type representation

namespace angara {

    // --- NEW: ModuleType to represent the public API of a module ---
    struct ModuleType : Type {
        const std::string name;
        // A map from exported symbol name to its Type.
        std::map<std::string, std::shared_ptr<Type>> exports;

        explicit ModuleType(std::string name)
            : Type(TypeKind::MODULE), name(std::move(name)) {}

        std::string toString() const override { return "module<" + name + ">"; }
    };


    class CompilerDriver {
    public:
        CompilerDriver();
        bool compile(const std::string& root_file_path);
        std::shared_ptr<ModuleType> resolveModule(const std::string& path, const Token& import_token);
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
    };

} // namespace angara