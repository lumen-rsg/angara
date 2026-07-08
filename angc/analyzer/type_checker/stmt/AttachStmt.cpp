#include "TypeChecker.h"
namespace angara {

    void TypeChecker::visit(std::shared_ptr<const AttachStmt> stmt) {
    }

    void TypeChecker::resolveAttach(const AttachStmt& stmt) {
        std::string module_path = stmt.modulePath.lexeme;

        std::shared_ptr<ModuleType> module_type = m_driver.resolveModule(module_path, stmt.modulePath);

        if (!module_type) {
            return;
        }

        // --kernel: native modules (.so/.dylib/.dll) are loaded via dlopen at
        // resolve time, which has no equivalent in a kernel. Refuse them.
        // Pure-Angara-source modules are compiled in and remain allowed.
        if (m_is_in_kernel_mode && module_type->is_native) {
            error(stmt.modulePath, "attach of native module '" + module_path + "' is not allowed in --kernel mode (a kernel has no dynamic loader). Use a pure-Angara module or declare the dependency via 'foreign func'.", "E904");
            return;
        }

        m_module_resolutions[&stmt] = module_type;

        if (!stmt.names.empty()) {
            for (const auto& name_token : stmt.names) {
                const std::string& name_str = name_token.lexeme;
                auto export_it = module_type->exports.find(name_str);

                if (export_it == module_type->exports.end()) {
                    error(name_token, "Module '" + module_type->name + "' has no exported member named '" + name_str + "'.", "E278");

                    std::vector<std::string> candidates;
                    for (const auto& [n, t] : module_type->exports) candidates.push_back(n);
                    find_and_report_suggestion(name_token, candidates);
                } else {
                    if (auto conflicting = m_symbols.declare(name_token, export_it->second, true, module_type)) {
                        error(name_token, "Symbol '" + name_str + "' is already declared.", "E279");
                        note(conflicting->declaration_token, "Previous declaration was here.");
                    }
                }
            }
        }
        else {
            std::string symbol_name;
            Token name_token;

            if (stmt.alias) {
                symbol_name = stmt.alias->lexeme;
                name_token = *stmt.alias;
            } else {
                symbol_name = CompilerDriver::get_base_name(stmt.modulePath.lexeme);
                name_token = Token(TokenType::IDENTIFIER, symbol_name, stmt.modulePath.line, 0);
            }

            if (auto conflicting = m_symbols.declare(name_token, module_type, true)) {
                 error(name_token, "Symbol '" + symbol_name + "' is already declared.", "E280");
                 note(conflicting->declaration_token, "Previous declaration was here.");
            }
        }
    }

}
