#include "TypeChecker.h"
namespace angara {

void TypeChecker::visit(std::shared_ptr<const VarDeclStmt> stmt) {
    std::shared_ptr<Type> final_type = nullptr;

    // We now have three scenarios to handle based on what the parser found.
    // The parser guarantees that at least one of (typeAnnotation, initializer) exists.

    if (stmt->typeAnnotation && stmt->initializer) {
        // --- CASE A: Both annotation and initializer are present ---
        // This is the most complex case, requiring a compatibility check.

        final_type = resolveType(stmt->typeAnnotation);

        stmt->initializer->accept(*this);
        auto initializer_type = popType();

        if (final_type->kind != TypeKind::ERROR && initializer_type->kind != TypeKind::ERROR) {
            bool types_match = check_type_compatibility(final_type, initializer_type);

            // Allow implicit narrowing for integer literals.
            if (!types_match && isInteger(final_type) && initializer_type->toString() == "i64") {
                if (std::dynamic_pointer_cast<const Literal>(stmt->initializer)) {
                    types_match = true;
                }
            }

            if (!types_match) {
                error(stmt->name, "Type mismatch. Variable is annotated as '" +
                    final_type->toString() + "' but is initialized with a value of type '" +
                    initializer_type->toString() + "'.");
                final_type = m_type_error; // Mark as an error to prevent cascading issues.
            }
        }

    } else if (stmt->initializer) {
        // --- CASE B: Initializer is present, but no type annotation (Type Inference) ---

        stmt->initializer->accept(*this);
        auto initializer_type = popType();
        final_type = initializer_type;

        // Issue an informational note about the inferred type.
        if (final_type->kind != TypeKind::ERROR && final_type->kind != TypeKind::NIL) {
            note(stmt->name, "Type for '" + stmt->name.lexeme + "' inferred as '" + final_type->toString() + "'. Consider adding an explicit annotation for clarity.");
        }

    } else { // Implies (stmt->typeAnnotation && !stmt->initializer)
        // --- CASE C: Only a type annotation is present ---
        // This is the simplest case.
        final_type = resolveType(stmt->typeAnnotation);
    }

    // The rest of your original logic remains the same, but uses `final_type`.
    m_variable_types[stmt.get()] = final_type;

    if (auto conflicting_symbol = m_symbols.declare(stmt->name, final_type, stmt->is_const)) {
        error(stmt->name, "re-declaration of variable '" + stmt->name.lexeme + "'.");
        note(conflicting_symbol->declaration_token, "previous declaration was here.");
    }

    if (stmt->is_exported) {
        if (m_symbols.getScopeDepth() > 0) {
            error(stmt->name, "'export' can only be used on top-level declarations.");
        } else {
            m_module_type->exports[stmt->name.lexeme] = final_type;
        }
    }

}

}