#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const AssignExpr& expr) {
        expr.value->accept(*this);
        const auto rhs_type = popType();

        // LANG-10: destructuring assignment — (a, b) = tuple_expr
        if (const auto tuple_target = std::dynamic_pointer_cast<const TupleExpr>(expr.target)) {
            if (rhs_type->kind != TypeKind::TUPLE) {
                error(expr.op, "Cannot destructure a value of type '" + rhs_type->toString() +
                               "' — destructuring assignment requires a tuple type on the right-hand side.", "E385");
                pushAndSave(&expr, m_type_error);
                return {};
            }
            auto tuple_type = std::dynamic_pointer_cast<TupleType>(rhs_type);
            if (tuple_type->element_types.size() != tuple_target->elements.size()) {
                error(expr.op, "Destructuring arity mismatch. The tuple type has " +
                               std::to_string(tuple_type->element_types.size()) +
                               " element(s), but " +
                               std::to_string(tuple_target->elements.size()) +
                               " variable(s) were given.", "E386");
                pushAndSave(&expr, m_type_error);
                return {};
            }
            for (size_t i = 0; i < tuple_target->elements.size(); ++i) {
                auto var_expr = std::dynamic_pointer_cast<const VarExpr>(tuple_target->elements[i]);
                if (!var_expr) {
                    error(expr.op, "Destructuring assignment targets must be simple variable names.", "E385");
                    pushAndSave(&expr, m_type_error);
                    return {};
                }
                // Resolve the variable's declared type
                auto symbol = m_symbols.resolve(var_expr->name.lexeme);
                if (!symbol) {
                    error(var_expr->name, "Variable '" + var_expr->name.lexeme + "' is not declared.", "E387");
                    pushAndSave(&expr, m_type_error);
                    return {};
                }
                if (symbol->is_const) {
                    error(var_expr->name, "Cannot assign to 'const' variable '" + symbol->name + "' in destructuring.", "E320");
                    note(symbol->declaration_token, "'" + symbol->name + "' was declared 'const' here.");
                }
                // Check that the tuple element type is compatible with the variable's declared type
                auto var_declared_type = symbol->type;
                auto element_type = tuple_type->element_types[i];
                if (var_declared_type && element_type &&
                    !check_type_compatibility(var_declared_type, element_type)) {
                    error(var_expr->name, "Type mismatch in destructuring position " +
                                          std::to_string(i) + ". Cannot assign element of type '" +
                                          element_type->toString() + "' to variable '" +
                                          var_expr->name.lexeme + "' of type '" +
                                          var_declared_type->toString() + "'.", "E319");
                }
            }
            pushAndSave(&expr, rhs_type);
            return {};
        }

        if (const auto subscript_target = std::dynamic_pointer_cast<const SubscriptExpr>(expr.target)) {
            subscript_target->object->accept(*this);
            const auto collection_type = popType();
            subscript_target->index->accept(*this);
            const auto index_type = popType();

            if (collection_type->kind == TypeKind::ERROR || index_type->kind == TypeKind::ERROR) {
                pushAndSave(&expr, m_type_error); return {};
            }

            if (collection_type->kind == TypeKind::LIST) {
                const auto list_type = std::dynamic_pointer_cast<ListType>(collection_type);
                if (!isInteger(index_type)) {
                    error(subscript_target->bracket, "List index must be an integer, but got '" + index_type->toString() + "'.", "E314");
                }
                if (!sameType(list_type->element_type, rhs_type)) {
                    error(expr.op, "Cannot assign a value of type '" + rhs_type->toString() + "' to a list element of type '" + list_type->element_type->toString() + "'.", "E315");
                }
            }
            else if (collection_type->kind == TypeKind::RECORD) {
                auto record_type = std::dynamic_pointer_cast<RecordType>(collection_type);
                if (index_type->toString() != "string") {
                    error(subscript_target->bracket, "Record key for assignment must be a string, but got '" + index_type->toString() + "'.", "E316");
                } else {
                    if (record_type->fields.empty()) {
                        // Dynamic field addition on generic record — always valid.
                    } else {
                        if (auto key_literal = std::dynamic_pointer_cast<const Literal>(subscript_target->index)) {
                            auto field_it = record_type->fields.find(key_literal->token.lexeme);
                            if (field_it == record_type->fields.end()) {
                                error(key_literal->token, "Record has no field named '" + key_literal->token.lexeme + "'.", "E317");
                            } else if (!check_type_compatibility(field_it->second, rhs_type)) {
                                error(expr.op, "Cannot assign a value of type '" + rhs_type->toString() + "' to field '" + key_literal->token.lexeme + "' which expects type '" + field_it->second->toString() + "'.", "E318");
                            }
                        }
                    }
                }
            }
            pushAndSave(&expr, rhs_type);
            return {};
        }

        expr.target->accept(*this);
        auto lhs_type = popType();

        if (rhs_type->kind == TypeKind::ERROR || lhs_type->kind == TypeKind::ERROR) {
            pushAndSave(&expr, m_type_error);
            return {};
        }

        if (!check_type_compatibility(lhs_type, rhs_type,
                std::dynamic_pointer_cast<const Literal>(expr.value).get())) {
            bool types_match = false;
            if (m_is_in_unsafe_context && rhs_type->kind == TypeKind::ANY) {
                types_match = true;
            }

            if (!types_match) {
                error(expr.op, "Type mismatch. Cannot assign a value of type '" +
                               displayType(*rhs_type, *lhs_type) + "' to a target of type '" +
                               displayType(*lhs_type, *rhs_type) + "'.", "E319");
            }
        }

        if (const auto var_target = std::dynamic_pointer_cast<const VarExpr>(expr.target)) {
            if (const auto symbol = m_symbols.resolve(var_target->name.lexeme); symbol && symbol->is_const) {
                error(var_target->name, "Cannot assign to 'const' variable '" + symbol->name + "'.", "E320");
                note(symbol->declaration_token, "'" + symbol->name + "' was declared 'const' here.");
            }
            // LANG-11: if assigning a lambda with defaults, register them under
            // the variable name so calls through this variable can use defaults.
            if (auto* lambda = dynamic_cast<const LambdaExpr*>(expr.value.get())) {
                bool has_defaults = false;
                for (const auto& d : lambda->param_defaults) {
                    if (d) { has_defaults = true; break; }
                }
                if (has_defaults) {
                    std::vector<std::string> names;
                    names.reserve(lambda->param_names.size());
                    for (const auto& pn : lambda->param_names) names.push_back(pn.lexeme);
                    m_function_param_names[var_target->name.lexeme] = std::move(names);
                    m_function_defaults[var_target->name.lexeme] = lambda->param_defaults;
                }
            }
        }

        else if (const auto get_target = std::dynamic_pointer_cast<const GetExpr>(expr.target)) {
            get_target->object->accept(*this);

            if (const auto object_type = popType(); object_type->kind == TypeKind::INSTANCE) {
                const auto instance_type = std::dynamic_pointer_cast<InstanceType>(object_type);
                const std::string& field_name = get_target->name.lexeme;

                if (const ClassType::MemberInfo* field_info = instance_type->class_type->findProperty(field_name); field_info == nullptr) {
                    error(get_target->name, "Class '" + instance_type->toString() +
                                            "' has no field named '" + field_name + "'.", "E321");
                } else {
                    if (instance_type->class_type->methods.contains(field_name)) {
                        error(get_target->name, "Cannot assign to method '" + field_name + "' — methods are not assignable.", "E322");
                    } else {
                        if (field_info->is_const) {
                            error(get_target->name, "Cannot assign to 'const' field '" + field_name + "'.", "E323");
                        }
                    }
                }
            }
        }

        pushAndSave(&expr, rhs_type);
        return {};
    }

}
