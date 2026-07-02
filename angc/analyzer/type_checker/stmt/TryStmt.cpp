#include "TypeChecker.h"
namespace angara {

    void TypeChecker::visit(std::shared_ptr<const TryStmt> stmt) {
        stmt->tryBlock->accept(*this, stmt->tryBlock);

        m_symbols.enterScope();

        std::shared_ptr<Type> exception_var_type;
        if (stmt->catchType) {
            exception_var_type = resolveType(stmt->catchType);
        } else {
            exception_var_type = m_type_any;
        }

        if (auto conflicting_symbol = m_symbols.declare(stmt->catchName, exception_var_type, true)) {
            error(stmt->catchName, "Symbol '" + stmt->catchName.lexeme + "' is already declared.", "E313");
            note(conflicting_symbol->declaration_token, "Previous declaration was here.");
        }

        stmt->catchBlock->accept(*this, stmt->catchBlock);

        exitScopeAndWarn();

        // v5: finally block (runs on both normal and catch paths).
        if (stmt->finallyBlock) {
            stmt->finallyBlock->accept(*this, stmt->finallyBlock);
        }
    }

}
