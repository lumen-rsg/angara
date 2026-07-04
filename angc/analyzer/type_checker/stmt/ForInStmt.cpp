#include "TypeChecker.h"
namespace angara {

    void TypeChecker::visit(std::shared_ptr<const ForInStmt> stmt) {
        stmt->collection->accept(*this);
        auto collection_type = popType();

        std::shared_ptr<Type> item_type = m_type_error;
        if (collection_type->kind == TypeKind::LIST) {
            item_type = std::dynamic_pointer_cast<ListType>(collection_type)->element_type;
        } else if (collection_type->toString() == "string") {
            item_type = m_type_string;
        } else if (collection_type->kind == TypeKind::ANY) {
            if (m_is_in_unsafe_context) {
                item_type = m_type_any;
            } else {
                error(stmt->name, "Iterating over a value of type 'any' requires an '@unsafe' block.", "E261");
            }
        }
        else {
            error(stmt->name, "Cannot iterate over type '" + collection_type->toString() + "'. Only lists and strings are iterable.", "E262");
        }

        m_symbols.enterScope();

        // LANG-10: destructuring for-in — element must be a tuple type
        if (!stmt->destructure_names.empty()) {
            if (item_type->kind == TypeKind::TUPLE) {
                auto tuple_type = std::dynamic_pointer_cast<TupleType>(item_type);
                if (tuple_type->element_types.size() != stmt->destructure_names.size()) {
                    error(stmt->destructure_names[0],
                        "Destructuring arity mismatch. The iteration element type is " +
                        item_type->toString() + " (" +
                        std::to_string(tuple_type->element_types.size()) +
                        " element(s)) but the pattern expects " +
                        std::to_string(stmt->destructure_names.size()) + ".",
                        "E386");
                } else {
                    for (size_t i = 0; i < stmt->destructure_names.size(); ++i) {
                        auto elem_type = tuple_type->element_types[i];
                        if (auto conflicting = m_symbols.declare(stmt->destructure_names[i], elem_type, true)) {
                            error(stmt->destructure_names[i],
                                "Symbol '" + stmt->destructure_names[i].lexeme + "' is already declared.",
                                "E263");
                            note(conflicting->declaration_token, "Previous declaration was here.");
                        }
                    }
                }
            } else if (item_type->kind == TypeKind::LIST) {
                // If the element type is a list, treat each position as `any`
                // (lists can have heterogeneous elements at runtime)
                for (size_t i = 0; i < stmt->destructure_names.size(); ++i) {
                    if (auto conflicting = m_symbols.declare(stmt->destructure_names[i], m_type_any, true)) {
                        error(stmt->destructure_names[i],
                            "Symbol '" + stmt->destructure_names[i].lexeme + "' is already declared.",
                            "E263");
                        note(conflicting->declaration_token, "Previous declaration was here.");
                    }
                }
            } else if (item_type->kind != TypeKind::ERROR) {
                error(stmt->destructure_names[0],
                    "Cannot destructure iteration element of type '" + item_type->toString() +
                    "'. Destructuring in 'for-in' requires the iteration element to be a tuple type like (i64, string).",
                    "E385");
            }
        } else {
            if (auto conflicting_symbol = m_symbols.declare(stmt->name, item_type, true)) {
                error(stmt->name, "Symbol '" + stmt->name.lexeme + "' is already declared.", "E263");
                note(conflicting_symbol->declaration_token, "Previous declaration was here.");
            }
        }

        m_loop_depth++;
        stmt->body->accept(*this, stmt->body);
        m_loop_depth--;

        exitScopeAndWarn();
    }

}
