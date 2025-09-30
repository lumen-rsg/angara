#include "TypeChecker.h"
namespace angara {

    void TypeChecker::visit(std::shared_ptr<const VarDeclStmt> stmt) {
    std::shared_ptr<Type> declared_type = nullptr;

    // 1. First, check if there's an initializer. This is now the primary source of type info.
    if (stmt->initializer) {
        stmt->initializer->accept(*this);
        auto initializer_type = popType();

        if (stmt->typeAnnotation) {
            // --- CASE A: Both annotation and initializer are present ---
            declared_type = resolveType(stmt->typeAnnotation);
            if (declared_type->kind != TypeKind::ERROR && initializer_type->kind != TypeKind::ERROR) {
                // Check for compatibility (using our existing robust logic)
                bool types_match = check_type_compatibility(declared_type, initializer_type);
                if (!types_match && isInteger(declared_type) && initializer_type->toString() == "i64") {
                    if (std::dynamic_pointer_cast<const Literal>(stmt->initializer)) {
                        types_match = true;
                    }
                }
                if (!types_match) {
                    error(stmt->name, "Type mismatch. Variable is annotated as '" +
                        declared_type->toString() + "' but is initialized with a value of type '" +
                        initializer_type->toString() + "'.");
                    declared_type = m_type_error;
                }
            }
        } else {
            // --- CASE B: Type Inference is used ---
            // The declared type is the type of the initializer.
            declared_type = initializer_type;

            // --- THIS IS THE NEW FEATURE ---
            // Issue an informational note about the inferred type.
            if (declared_type->kind != TypeKind::ERROR && declared_type->kind != TypeKind::NIL) {
                note(stmt->name, "Type for '" + stmt->name.lexeme + "' was inferred as '" + declared_type->toString() + "'. Consider adding an explicit annotation for clarity: `as " + declared_type->toString() + "`");
            }
            // --- END NEW FEATURE ---
        }
    } else {
        // --- CASE C: Only a type annotation is present ---
        // The parser guarantees this case.
        declared_type = resolveType(stmt->typeAnnotation);
    }

    // 2. The rest of the function is unchanged.
    m_variable_types[stmt.get()] = declared_type;
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