#include "TypeChecker.h"

namespace angara {

    void TypeChecker::visit(std::shared_ptr<const DropStmt> stmt) {
        // v5: drop — verify the target is valid. The Chaperone pass (Stage 3)
        // will verify it's an owned type, not double-dropped, and not used after.
        //
        // H8: support both `drop x` (VarExpr) and `drop this.field` (GetExpr).
        if (auto* ve = dynamic_cast<const VarExpr*>(stmt->target.get())) {
            auto sym = m_symbols.resolve(ve->name.lexeme);
            if (!sym) {
                error(ve->name, "Cannot drop '" + ve->name.lexeme +
                      "' — variable not found.", "E503");
            }
        } else {
            // Field access or other expression: type-check it to verify
            // the expression is well-formed (field exists, types align, etc.).
            stmt->target->accept(*this);
        }
    }

}
