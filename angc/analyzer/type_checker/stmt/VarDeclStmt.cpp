#include "TypeChecker.h"
namespace angara {

void TypeChecker::visit(std::shared_ptr<const VarDeclStmt> stmt) {
    std::shared_ptr<Type> final_type = nullptr;

    // Foreign const: resolve type only, no initializer
    if (stmt->is_foreign) {
        final_type = resolveType(stmt->typeAnnotation);
        m_variable_types[stmt.get()] = final_type;
        if (auto conflicting_symbol = m_symbols.declare(stmt->name, final_type, true)) {
            error(stmt->name, "Symbol '" + stmt->name.lexeme + "' is already declared.", "E274");
            note(conflicting_symbol->declaration_token, "Previous declaration was here.");
        }
        return;
    }

    if (stmt->typeAnnotation && stmt->initializer) {
        final_type = resolveType(stmt->typeAnnotation);

        // Pass expected type down for bidirectional inference (e.g., list<i64> -> [])
        auto saved_expected = m_expected_type;
        m_expected_type = final_type;
        stmt->initializer->accept(*this);
        m_expected_type = saved_expected;

        auto initializer_type = popType();

        if (final_type->kind != TypeKind::ERROR && initializer_type->kind != TypeKind::ERROR) {
            // TS-3: pass a bare integer literal so in-range narrowing (e.g.
            // `let b as u8 = 200;`) is permitted; out-of-range still errors via
            // the predicate returning false.
            const Literal* init_lit = std::dynamic_pointer_cast<const Literal>(stmt->initializer).get();
            bool types_match = check_type_compatibility(final_type, initializer_type, init_lit);

            if (!types_match) {
                error(stmt->name, "Type mismatch. Variable is annotated as '" +
                    displayType(*final_type, *initializer_type) + "' but is initialized with a value of type '" +
                    displayType(*initializer_type, *final_type) + "'.", "E275");
                final_type = m_type_error;
            }
        }

    } else if (stmt->initializer) {
        stmt->initializer->accept(*this);
        auto initializer_type = popType();
        final_type = initializer_type;

    } else {
        final_type = resolveType(stmt->typeAnnotation);
    }

    m_variable_types[stmt.get()] = final_type;

    if (auto conflicting_symbol = m_symbols.declare(stmt->name, final_type, stmt->is_const)) {
        error(stmt->name, "Symbol '" + stmt->name.lexeme + "' is already declared.", "E276");
        note(conflicting_symbol->declaration_token, "Previous declaration was here.");
    }

    if (stmt->is_exported) {
        if (m_symbols.getScopeDepth() > 0) {
            error(stmt->name, "'export' can only be used on top-level declarations.", "E277");
        } else {
            m_module_type->exports[stmt->name.lexeme] = final_type;
        }
    }

}

}
