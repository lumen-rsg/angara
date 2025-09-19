//
// Created by cv2 on 9/19/25.
//

#include "TypeChecker.h"
namespace angara {

    void TypeChecker::visit(std::shared_ptr<const VarDeclStmt> stmt) {
        std::shared_ptr<Type> initializer_type = nullptr;
        if (stmt->initializer) {
            stmt->initializer->accept(*this);
            initializer_type = popType();
        }

        std::shared_ptr<Type> declared_type = nullptr;
        if (stmt->typeAnnotation) {
            declared_type = resolveType(stmt->typeAnnotation);
        }

        // --- Logic for type inference and error checking (unchanged) ---
        if (!declared_type && initializer_type) {
            declared_type = initializer_type;
        } else if (declared_type && !initializer_type) {
            // This is fine
        } else if (!declared_type && !initializer_type) {
            error(stmt->name, "Cannot declare a variable without a type annotation or an initializer.");
            declared_type = m_type_error;
        } else if (declared_type && initializer_type) {
            bool types_match = (declared_type->toString() == initializer_type->toString());
            if (!types_match && initializer_type->toString() == "list<any>" && declared_type->kind == TypeKind::LIST) {
                if (auto list_expr = std::dynamic_pointer_cast<const ListExpr>(stmt->initializer)) {
                    if (list_expr->elements.empty()) {
                        types_match = true;
                    }
                }
            }
            if (!types_match && isInteger(declared_type) && initializer_type->toString() == "i64") {
                types_match = true;
            }
            if (!types_match && initializer_type->kind == TypeKind::ANY) {
                types_match = true; // It's always safe to assign 'any' to a typed variable.
            }

            // Rule 2: It's safe to assign a generic record '{}' from a native
            // function to a specifically typed record variable. This is a type assertion.
            if (!types_match && declared_type->kind == TypeKind::RECORD && initializer_type->kind == TypeKind::RECORD) {
                auto initializer_record = std::dynamic_pointer_cast<RecordType>(initializer_type);
                if (initializer_record->fields.empty()) {
                    types_match = true;
                }
            }

            if (!types_match) {
                error(stmt->name, "Type mismatch. Variable is annotated as '" +
                    declared_type->toString() + "' but is initialized with a value of type '" +
                    initializer_type->toString() + "'.");
                declared_type = m_type_error;
            }

            if (!types_match && declared_type->kind == TypeKind::OPTIONAL) {
                auto optional_type = std::dynamic_pointer_cast<OptionalType>(declared_type);
                // You can assign a T to a T?
                if (optional_type->wrapped_type->toString() == initializer_type->toString()) {
                    types_match = true;
                }
                // You can assign `nil` to a T?
                if (initializer_type->kind == TypeKind::NIL) {
                    types_match = true;
                }
            }
        }

        m_variable_types[stmt.get()] = declared_type;

        // Declare the symbol in the current scope
        if (auto conflicting_symbol = m_symbols.declare(stmt->name, declared_type, stmt->is_const)) {
            error(stmt->name, "re-declaration of variable '" + stmt->name.lexeme + "'.");
            note(conflicting_symbol->declaration_token, "previous declaration was here.");
        }

        if (stmt->is_exported) {
            if (m_symbols.getScopeDepth() > 0) {
                error(stmt->name, "'export' can only be used on top-level declarations.");
            } else {
                m_module_type->exports[stmt->name.lexeme] = declared_type;
            }
        }
    }

}