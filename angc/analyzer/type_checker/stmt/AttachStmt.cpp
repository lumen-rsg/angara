#include "TypeChecker.h"
namespace angara {

    void TypeChecker::resolveAttach(const AttachStmt& stmt) {
        std::string module_path = stmt.modulePath.lexeme;

        // 1. Resolve module using driver
        // This triggers compilation of the dependency if needed
        std::shared_ptr<ModuleType> module_type = m_driver.resolveModule(module_path, stmt.modulePath);

        if (!module_type) {
            // Driver already reported the error (e.g., file not found).
            return;
        }

        // Store resolution for transpiler
        m_module_resolutions[&stmt] = module_type;

        // --- Selective Import (e.g., 'attach get_channel from rmq') ---
        if (!stmt.names.empty()) {
            for (const auto& name_token : stmt.names) {
                const std::string& name_str = name_token.lexeme;
                auto export_it = module_type->exports.find(name_str);

                if (export_it == module_type->exports.end()) {
                    error(name_token, "Module '" + module_type->name + "' has no exported member named '" + name_str + "'.");
                } else {
                    // FIX: Pass the 'module_type' here so the Symbol knows where it came from!
                    if (auto conflicting = m_symbols.declare(name_token, export_it->second, true, module_type)) {
                        error(name_token, "re-declaration of symbol '" + name_str + "'.");
                        note(conflicting->declaration_token, "previous declaration was here.");
                    }
                }
            }
        }
        // --- Whole Module Import (e.g., 'attach rmq') ---
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

            // Here, from_module is nullptr because the symbol *is* the module itself. This is correct.
            if (auto conflicting = m_symbols.declare(name_token, module_type, true)) {
                 error(name_token, "re-declaration of symbol '" + symbol_name + "'.");
                 note(conflicting->declaration_token, "previous declaration was here.");
            }
        }
    }

    void TypeChecker::visit(std::shared_ptr<const AttachStmt> stmt) {
        // Handled entirely in the pre-pass. This visit is now a no-op.
    }
}