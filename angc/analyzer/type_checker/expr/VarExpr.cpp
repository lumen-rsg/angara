//
// Created by cv2 on 9/19/25.
//

#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const VarExpr& expr) {
        // Use our new helper that understands type narrowing.
        auto symbol = resolve_and_narrow(expr);

        if (!symbol) {
            error(expr.name, "Undefined variable '" + expr.name.lexeme + "'.");

            // --- ADD SUGGESTION LOGIC ---
            std::vector<std::string> candidates;
            // Walk up the scope chain to gather all visible symbols.
            for (const auto& scope : m_symbols.getScopes()) { // <-- You will need to add a getScopes() method to SymbolTable
                for (const auto& [name, sym] : scope) {
                    candidates.push_back(name);
                }
            }
            find_and_report_suggestion(expr.name, candidates);
            // --- END SUGGESTION LOGIC ---

            pushAndSave(&expr, m_type_error);
        } else {
            // Save the original resolution for the transpiler (it doesn't care about narrowing).
            m_variable_resolutions[&expr] = m_symbols.resolve(expr.name.lexeme);
            // Push the potentially narrowed type for type checking.
            pushAndSave(&expr, symbol->type);
        }
        return {};
    }

}