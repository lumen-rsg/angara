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

        if (auto conflicting_symbol = m_symbols.declare(stmt->name, item_type, true)) {
            error(stmt->name, "Symbol '" + stmt->name.lexeme + "' is already declared.", "E263");
            note(conflicting_symbol->declaration_token, "Previous declaration was here.");
        }

        m_loop_depth++;
        stmt->body->accept(*this, stmt->body);
        m_loop_depth--;

        exitScopeAndWarn();
    }

}
