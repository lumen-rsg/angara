//
// Created by cv2 on 9/19/25.
//

#include "TypeChecker.h"
namespace angara {

    void TypeChecker::visit(std::shared_ptr<const ForInStmt> stmt) {
        // 1. First, type check the expression that provides the collection.
        stmt->collection->accept(*this);
        auto collection_type = popType();

        // 2. Determine the type of the items IN the collection.
        std::shared_ptr<Type> item_type = m_type_error; // Default to error
        if (collection_type->kind == TypeKind::LIST) {
            // If it's a list<T>, the item type is T.
            item_type = std::dynamic_pointer_cast<ListType>(collection_type)->element_type;
        } else if (collection_type->toString() == "string") {
            // If it's a string, the item type is also string (for each character).
            item_type = m_type_string;
        } else {
            error(stmt->name, "The 'for..in' loop can only iterate over a list or a string, but got '" +
                              collection_type->toString() + "'.");
        }

        // 3. The entire loop introduces a new scope for the loop variable.
        m_symbols.enterScope();

        // 4. Declare the loop variable (e.g., 'ability_obj') inside this new scope.
        //    It is a constant for each iteration.
        if (auto conflicting_symbol = m_symbols.declare(stmt->name, item_type, true)) {
            // This case should be logically impossible if enterScope() works correctly,
            // as the scope has just been created. But we handle it for robustness.
            error(stmt->name, "compiler internal error: re-declaration of loop variable '" + stmt->name.lexeme + "'.");
            note(conflicting_symbol->declaration_token, "previous declaration was here.");
        }

        // 5. Now that the loop variable is in scope, type check the body of the loop.
        //    The body executes within the new scope.
        m_loop_depth++;
        stmt->body->accept(*this, stmt->body);
        m_loop_depth--;

        // 6. When the loop is finished, exit its scope, destroying the loop variable.
        m_symbols.exitScope();
    }

}