#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const VarExpr& expr) {
        auto symbol = resolve_and_narrow(expr);

        if (!symbol) {
            error(expr.name, "Undefined variable '" + expr.name.lexeme + "'.");

            std::vector<std::string> candidates;
            for (const auto& scope : m_symbols.getScopes()) {
                for (const auto& [name, sym] : scope) {
                    candidates.push_back(name);
                }
            }
            find_and_report_suggestion(expr.name, candidates);

            pushAndSave(&expr, m_type_error);
        } else {
            m_variable_resolutions[&expr] = m_symbols.resolve(expr.name.lexeme);
            pushAndSave(&expr, symbol->type);
        }
        return {};
    }

}
