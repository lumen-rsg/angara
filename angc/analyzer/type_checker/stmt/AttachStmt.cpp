//
// Created by cv2 on 9/19/25.
//
#include "TypeChecker.h"
namespace angara {

    void TypeChecker::resolveAttach(const AttachStmt& stmt) {
        std::string module_path = stmt.modulePath.lexeme;
        std::shared_ptr<ModuleType> module_type = m_driver.resolveModule(module_path, stmt.modulePath);

        if (!module_type) {
            // Driver already reported the error (e.g., file not found).
            return;
        }

        m_module_resolutions[&stmt] = module_type;

        // --- Case 1: Selective import (e.g., `attach connect from websocket`) ---
        if (!stmt.names.empty()) {
            for (const auto& name_token : stmt.names) {
                const std::string& name_str = name_token.lexeme;
                auto export_it = module_type->exports.find(name_str);

                if (export_it == module_type->exports.end()) {
                    error(name_token, "Module '" + module_type->name + "' has no exported member named '" + name_str + "'.");
                } else {
                    // Declare the new symbol, PASSING the origin module.
                    if (auto conflicting_symbol = m_symbols.declare(name_token, export_it->second, true, module_type)) {
                        error(name_token, "re-declaration of symbol '" + name_str + "'.");
                        note(conflicting_symbol->declaration_token, "previous declaration was here.");
                    }
                }
            }
        }
        // --- Case 2: Whole-module import (e.g., `attach fs`) ---
        else {
            std::string symbol_name;
            Token name_token;

            if (stmt.alias) {
                symbol_name = stmt.alias->lexeme;
                name_token = *stmt.alias;
            } else {
                // For logical names like "fs", the symbol is "fs". For paths, it's the basename.
                symbol_name = CompilerDriver::get_base_name(stmt.modulePath.lexeme);
                name_token = Token(TokenType::IDENTIFIER, symbol_name, stmt.modulePath.line, 0);
            }

            // Declare the module symbol. It has no origin module (it *is* the module).
            if (auto conflicting_symbol = m_symbols.declare(name_token, module_type, true)) {
                 error(name_token, "re-declaration of symbol '" + symbol_name + "'.");
                 note(conflicting_symbol->declaration_token, "previous declaration was here.");
            }
        }
    }

    void TypeChecker::visit(std::shared_ptr<const AttachStmt> stmt) {
        // handled in the passes.
    }

}
