#include "CTranspiler.h"

namespace angara {

    std::string CTranspiler::transpileVarExpr(const VarExpr& expr) {
        auto symbol_it = m_type_checker.m_variable_resolutions.find(&expr);

        if (symbol_it == m_type_checker.m_variable_resolutions.end()) {
            // Handle module references (e.g., the 'rmq' in 'rmq.get_channel')
            auto symbol = m_type_checker.m_symbols.resolve(expr.name.lexeme);
            if (symbol && symbol->type->kind == TypeKind::MODULE) {
                return sanitize_name(expr.name.lexeme);
            }
            return "/* unresolved: " + expr.name.lexeme + " */";
        }

        auto symbol = symbol_it->second;

        // 1. Local Variables and Parameters
        // No module prefix for things on the stack.
        if (symbol->depth > 0) {
            return sanitize_name(symbol->name);
        }

        // 2. Global Symbols
        // Determine the origin module. If it was imported, use that module's name.
        // If it was defined in this file, use m_current_module_name.
        std::string origin_module = symbol->from_module ? symbol->from_module->name : m_current_module_name;

        if (symbol->type->kind == TypeKind::FUNCTION) {
            // Special case for main to keep the C linker happy with the entry point
            if (symbol->name == "main") return "g_angara_main_closure";

            // Standard closure name: g_[module]_[function]
            return "g_" + origin_module + "_" + sanitize_name(symbol->name);
        }

        // Standard global variable: [module]_[variable]
        return origin_module + "_" + sanitize_name(symbol->name);
    }

}