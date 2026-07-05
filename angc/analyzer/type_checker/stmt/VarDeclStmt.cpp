#include "TypeChecker.h"
namespace angara {

void TypeChecker::visit(std::shared_ptr<const VarDeclStmt> stmt) {
    // LANG-10: destructuring declaration — let (a, b) = expr;
    if (!stmt->destructure_names.empty()) {
        std::shared_ptr<Type> rhs_type = nullptr;

        if (stmt->typeAnnotation && stmt->initializer) {
            rhs_type = resolveType(stmt->typeAnnotation);
            auto saved_expected = m_expected_type;
            m_expected_type = rhs_type;
            stmt->initializer->accept(*this);
            m_expected_type = saved_expected;
            auto init_type = popType();
            if (rhs_type->kind != TypeKind::ERROR && init_type->kind != TypeKind::ERROR) {
                if (!check_type_compatibility(rhs_type, init_type)) {
                    error(stmt->destructure_names[0],
                        "Type mismatch. Destructuring target is annotated as '" +
                        displayType(*rhs_type, *init_type) + "' but is initialized with '" +
                        displayType(*init_type, *rhs_type) + "'.",
                        "E275");
                    rhs_type = m_type_error;
                }
            }
        } else if (stmt->initializer) {
            stmt->initializer->accept(*this);
            rhs_type = popType();
        } else {
            rhs_type = resolveType(stmt->typeAnnotation);
        }

        if (rhs_type->kind != TypeKind::TUPLE && rhs_type->kind != TypeKind::ERROR) {
            error(stmt->destructure_names[0],
                "Cannot destructure a value of type '" + rhs_type->toString() +
                "'. Destructuring requires a tuple type like (i64, string).",
                "E385");
            return;
        }

        if (rhs_type->kind == TypeKind::TUPLE) {
            auto tuple_type = std::dynamic_pointer_cast<TupleType>(rhs_type);
            if (tuple_type->element_types.size() != stmt->destructure_names.size()) {
                error(stmt->destructure_names[0],
                    "Destructuring arity mismatch. The tuple has " +
                    std::to_string(tuple_type->element_types.size()) +
                    " element(s) but the pattern expects " +
                    std::to_string(stmt->destructure_names.size()) + ".",
                    "E386");
                return;
            }

            for (size_t i = 0; i < stmt->destructure_names.size(); ++i) {
                auto elem_type = tuple_type->element_types[i];
                if (auto conflicting = m_symbols.declare(stmt->destructure_names[i], elem_type, stmt->is_const)) {
                    error(stmt->destructure_names[i],
                        "Symbol '" + stmt->destructure_names[i].lexeme + "' is already declared.",
                        "E276");
                    note(conflicting->declaration_token, "Previous declaration was here.");
                }
            }
        }
        return;
    }

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

        // LANG-11: if the initializer is a lambda with default args, register
        // them under the variable name so call resolution can find them.
        if (auto* lambda = dynamic_cast<const LambdaExpr*>(stmt->initializer.get())) {
            bool has_defaults = false;
            for (const auto& d : lambda->param_defaults) {
                if (d) { has_defaults = true; break; }
            }
            if (has_defaults) {
                std::vector<std::string> names;
                names.reserve(lambda->param_names.size());
                for (const auto& pn : lambda->param_names) names.push_back(pn.lexeme);
                m_function_param_names[stmt->name.lexeme] = std::move(names);
                m_function_defaults[stmt->name.lexeme] = lambda->param_defaults;
            }
        }

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
