#include "TypeChecker.h"

namespace angara {

    void TypeChecker::defineTypeAliasHeader(const TypeAliasStmt& stmt) {
        // Resolve the RHS type annotation to a semantic type.
        auto resolved_type = resolveType(stmt.aliased_type);
        if (resolved_type->kind == TypeKind::ERROR) {
            return;  // resolveType already reported the error
        }

        // Register the alias name in the symbol table pointing to the resolved type.
        if (auto conflicting = m_symbols.declare(stmt.name, resolved_type, true)) {
            error(stmt.name, "Symbol '" + stmt.name.lexeme + "' is already declared.", "E248");
            note(conflicting->declaration_token, "previous declaration was here.");
            return;
        }

        // If exported, add to module exports.
        if (stmt.is_exported) {
            m_module_type->exports[stmt.name.lexeme] = resolved_type;
        }
    }

    void TypeChecker::visit(std::shared_ptr<const TypeAliasStmt> stmt) {
    }

}
