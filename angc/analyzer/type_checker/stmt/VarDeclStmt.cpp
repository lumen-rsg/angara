#include "TypeChecker.h"
namespace angara {

    void TypeChecker::visit(std::shared_ptr<const VarDeclStmt> stmt) {
        // The parser now guarantees stmt->typeAnnotation is not null.
        // 1. Resolve the explicitly declared type.
        auto declared_type = resolveType(stmt->typeAnnotation);

        // 2. If an initializer exists, check that its type is compatible.
        if (stmt->initializer) {
            stmt->initializer->accept(*this);
            auto initializer_type = popType();

            // Prevent cascading errors.
            if (declared_type->kind != TypeKind::ERROR && initializer_type->kind != TypeKind::ERROR) {
                if (!check_type_compatibility(declared_type, initializer_type)) {
                    error(stmt->name, "Type mismatch. Cannot initialize variable of type '" +
                                      declared_type->toString() + "' with a value of type '" +
                                      initializer_type->toString() + "'.");
                    declared_type = m_type_error; // Mark as error to prevent further issues
                }
            }
        }

        // 3. Store the (now definitive) type for this variable.
        m_variable_types[stmt.get()] = declared_type;

        // 4. Declare the symbol in the current scope.
        if (auto conflicting_symbol = m_symbols.declare(stmt->name, declared_type, stmt->is_const)) {
            error(stmt->name, "re-declaration of variable '" + stmt->name.lexeme + "'.");
            note(conflicting_symbol->declaration_token, "previous declaration was here.");
        }

        // 5. Handle exporting the symbol (logic is unchanged).
        if (stmt->is_exported) {
            if (m_symbols.getScopeDepth() > 0) {
                error(stmt->name, "'export' can only be used on top-level declarations.");
            } else {
                m_module_type->exports[stmt->name.lexeme] = declared_type;
            }
        }
    }

}