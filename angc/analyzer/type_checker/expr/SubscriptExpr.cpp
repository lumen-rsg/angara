//
// Created by cv2 on 9/19/25.
//

#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const SubscriptExpr& expr) {
        // 1. Type check the object being subscripted and the index.
        expr.object->accept(*this);
        auto collection_type = popType();
        expr.index->accept(*this);
        auto index_type = popType();

        // 2. Bail out early if a sub-expression had an error.
        if (collection_type->kind == TypeKind::ERROR || index_type->kind == TypeKind::ERROR) {
            pushAndSave(&expr, m_type_error);
            return {};
        }

        std::shared_ptr<Type> result_type = m_type_error; // Default to error

        // --- Case 1: Accessing a list element ---
        if (collection_type->kind == TypeKind::LIST) {
            auto list_type = std::dynamic_pointer_cast<ListType>(collection_type);

            // Rule: List indices must be integers.
            if (!isInteger(index_type)) { // Use our isInteger helper for flexibility
                error(expr.bracket, "List index must be an integer, but got '" +
                                    index_type->toString() + "'.");
            } else {
                // Success! The result type is the list's element type.
                result_type = list_type->element_type;
            }
        }
            // --- Case 2: Accessing a record field ---
        else if (collection_type->kind == TypeKind::RECORD) {
            auto record_type = std::dynamic_pointer_cast<RecordType>(collection_type);
            if (index_type->toString() != "string") {
                error(expr.bracket, "Record key must be a string, but got '" + index_type->toString() + "'.");
            } else {
                // --- THIS IS THE FIX ---
                // If the record type is the generic, empty one, then all
                // key access is dynamic and results in a value of type `any`.
                if (record_type->fields.empty()) {
                    result_type = m_type_any;
                } else {
                    // Otherwise, it's a specifically-typed record. Apply the
                    // original logic for static checking.
                    if (auto key_literal = std::dynamic_pointer_cast<const Literal>(expr.index)) {
                        const std::string& key_name = key_literal->token.lexeme;
                        auto field_it = record_type->fields.find(key_name);
                        if (field_it == record_type->fields.end()) {
                            error(key_literal->token, "Record of type '" + record_type->toString() + "' has no statically-known field named '" + key_name + "'.");
                            result_type = m_type_error;
                        } else {
                            result_type = field_it->second; // Success!
                        }
                    } else {
                        // Dynamic access on a specific record type also results in `any`.
                        result_type = m_type_any;
                    }
                }
                // --- END OF FIX ---
            }
        }
            // --- Case 3: Accessing a string character ---
        else if (collection_type->toString() == "string") {
            // Rule: String indices must be integers.
            if (!isInteger(index_type)) {
                error(expr.bracket, "String index must be an integer, but got '" +
                                    index_type->toString() + "'.");
            } else {
                // Success! The result of subscripting a string is another string.
                result_type = m_type_string;
            }
        }
        else {
            error(expr.bracket, "Object of type '" + collection_type->toString() + "' is not subscriptable.");
        }

        // Finally, push and save the single, definitive result type for this expression.
        pushAndSave(&expr, result_type);
        return {};
    }

}