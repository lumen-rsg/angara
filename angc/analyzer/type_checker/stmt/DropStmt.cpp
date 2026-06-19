#include "TypeChecker.h"

namespace angara {

    void TypeChecker::visit(std::shared_ptr<const DropStmt> stmt) {
        // v5: drop — verify the variable exists. The Chaperone pass (Stage 3)
        // will verify it's an owned type, not double-dropped, and not used after.
        auto sym = m_symbols.resolve(stmt->name.lexeme);
        if (!sym) {
            error(stmt->name, "Cannot drop '" + stmt->name.lexeme +
                  "' — variable not found.", "E503");
        }
    }

}
