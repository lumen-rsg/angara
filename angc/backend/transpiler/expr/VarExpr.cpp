//
// Created by cv2 on 9/19/25.
//
#include "CTranspiler.h"
namespace angara {

    std::string CTranspiler::transpileVarExpr(const VarExpr& expr) {
        auto symbol_resolution = m_type_checker.m_variable_resolutions.find(&expr);
        if (symbol_resolution == m_type_checker.m_variable_resolutions.end()) {
            // This could be a reference to a module itself.
            auto symbol = m_type_checker.m_symbols.resolve(expr.name.lexeme);
            if (symbol && symbol->type->kind == TypeKind::MODULE) {
                // It's the module object. The transpiler doesn't need to do anything
                // special with it here; the GetExpr transpiler will handle it.
                return sanitize_name(expr.name.lexeme);
            }
            return "/* unresolved var: " + expr.name.lexeme + " */";
        }

        auto symbol = symbol_resolution->second;
        if (symbol->depth > 0) {
            // It's a local variable or a parameter.
            return sanitize_name(symbol->name);
        } else {
            // It's a global variable in the CURRENT module. Mangle it.
            // The closure for a function `parse` is `g_parse`. A global var `x` is `main_x`.
            if (symbol->type->kind == TypeKind::FUNCTION) {
                return "g_" + sanitize_name(symbol->name);
            }
            return m_current_module_name + "_" + sanitize_name(symbol->name);
        }
    }

}