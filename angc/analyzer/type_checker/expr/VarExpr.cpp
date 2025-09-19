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