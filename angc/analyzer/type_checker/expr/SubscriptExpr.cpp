#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const SubscriptExpr& expr) {
        expr.object->accept(*this);
        auto collection_type = popType();
        expr.index->accept(*this);
        auto index_type = popType();

        if (collection_type->kind == TypeKind::ERROR || index_type->kind == TypeKind::ERROR) {
            pushAndSave(&expr, m_type_error);
            return {};
        }

        std::shared_ptr<Type> result_type = m_type_error;

        if (collection_type->kind == TypeKind::LIST) {
            auto list_type = std::dynamic_pointer_cast<ListType>(collection_type);

            if (!isInteger(index_type)) {
                error(expr.bracket, "List index must be an integer, but got '" + index_type->toString() + "'.", "E345");
            } else {
                result_type = list_type->element_type;
            }
        }
        else if (collection_type->kind == TypeKind::ANY) {
            if (!m_is_in_unsafe_context) {
                error(expr.bracket, "Cannot subscript a value of type 'any' — this requires an '@unsafe' block.", "E346");
                result_type = m_type_error;
            } else {
                if (index_type->toString() != "string" && !isInteger(index_type)) {
                    error(expr.bracket, "Unsafe subscript on 'any' requires a string or integer index, but got '" + index_type->toString() + "'.", "E347");
                    result_type = m_type_error;
                } else {
                    result_type = m_type_any;
                }
            }
        }
        else if (collection_type->kind == TypeKind::RECORD) {
            auto record_type = std::dynamic_pointer_cast<RecordType>(collection_type);
            if (index_type->toString() != "string") {
                error(expr.bracket, "Record key must be a string, but got '" + index_type->toString() + "'.", "E348");
            } else {
                if (record_type->fields.empty()) {
                    result_type = m_type_any;
                } else {
                    if (auto key_literal = std::dynamic_pointer_cast<const Literal>(expr.index)) {
                        const std::string& key_name = key_literal->token.lexeme;
                        auto field_it = record_type->fields.find(key_name);
                        if (field_it == record_type->fields.end()) {
                            error(key_literal->token, "Record has no field named '" + key_name + "'.", "E349");
                            result_type = m_type_error;
                        } else {
                            result_type = field_it->second;
                        }
                    } else {
                        result_type = m_type_any;
                    }
                }
            }
        }
        else if (collection_type->kind == TypeKind::TUPLE) {
            // LANG-10: tuple subscript — index by position
            auto tuple_type = std::dynamic_pointer_cast<TupleType>(collection_type);
            if (!isInteger(index_type)) {
                error(expr.bracket, "Tuple index must be an integer, but got '" + index_type->toString() + "'.", "E352");
            } else if (auto idx_literal = std::dynamic_pointer_cast<const Literal>(expr.index)) {
                // Constant integer index: resolve the element type at that position
                int idx = std::stoi(idx_literal->token.lexeme);
                if (idx < 0 || static_cast<size_t>(idx) >= tuple_type->element_types.size()) {
                    error(expr.bracket, "Tuple index " + std::to_string(idx) +
                        " is out of bounds (tuple has " +
                        std::to_string(tuple_type->element_types.size()) + " element(s)).", "E353");
                } else {
                    result_type = tuple_type->element_types[idx];
                }
            } else {
                // Dynamic index: can't determine the element type statically
                result_type = m_type_any;
            }
        }
        else if (collection_type->toString() == "string") {
            if (!isInteger(index_type)) {
                error(expr.bracket, "String index must be an integer, but got '" + index_type->toString() + "'.", "E350");
            } else {
                result_type = m_type_string;
            }
        }
        else {
            error(expr.bracket, "Type '" + collection_type->toString() + "' does not support subscript access. Only lists, records, tuples, and strings are subscriptable.", "E351");
        }

        pushAndSave(&expr, result_type);
        return {};
    }

}
