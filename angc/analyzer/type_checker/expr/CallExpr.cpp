#include "TypeChecker.h"
#include <set>
namespace angara {

    std::any TypeChecker::visit(const CallExpr& expr) {
        expr.callee->accept(*this);
        auto callee_type = popType();
        std::vector<std::shared_ptr<Type>> arg_types;
        for (const auto& arg_expr : expr.arguments) {
            arg_expr->accept(*this);
            arg_types.push_back(popType());
        }
        if (m_hadError) { pushAndSave(&expr, m_type_error); return {}; }

        if (auto var_expr = std::dynamic_pointer_cast<const VarExpr>(expr.callee)) {
            if (var_expr->name.lexeme == "spawn") {
                check_spawn_call(expr, arg_types);
                pushAndSave(&expr, m_hadError ? m_type_error : m_type_thread);
                return {};
            }
        }

        std::shared_ptr<Type> result_type = m_type_error;

        if (callee_type->kind == TypeKind::FUNCTION) {
            auto func_type = std::dynamic_pointer_cast<FunctionType>(callee_type);
            check_function_call(expr, func_type, arg_types);
            if (!m_hadError) {
                result_type = func_type->return_type;
            }
        }
        else if (callee_type->kind == TypeKind::CLASS) {
            auto class_type = std::dynamic_pointer_cast<ClassType>(callee_type);
            const auto* init_prop = class_type->findProperty("init");

            if (!init_prop) {
                if (!arg_types.empty()) {
                    error(expr.paren, "Class '" + class_type->name + "' has no constructor that accepts arguments.");
                }
            } else {
                auto init_sig = std::dynamic_pointer_cast<FunctionType>(init_prop->type);
                check_function_call(expr, init_sig, arg_types);
            }

            if (!m_hadError) {
                result_type = std::make_shared<InstanceType>(class_type);
            }
        } else if (callee_type->kind == TypeKind::DATA) {
            auto data_type = std::dynamic_pointer_cast<DataType>(callee_type);
            check_function_call(expr, data_type->constructor_type, arg_types);
            if (!m_hadError) {
                result_type = data_type;
            }
        }
        else if (callee_type->kind == TypeKind::ANY) {
            if (m_is_in_unsafe_context) {
                result_type = m_type_any;
            } else {
                error(expr.paren, "Cannot call a value of type 'any' — this requires an '@unsafe' block.");
            }
        }
        else {
            error(expr.paren, "Expression of type '" + callee_type->toString() + "' is not callable. Only functions, classes, and data types can be called.");
        }

        pushAndSave(&expr, result_type);
        return {};
    }

    void TypeChecker::check_spawn_call(
            const CallExpr& call,
            const std::vector<std::shared_ptr<Type>>& arg_types
    ) {
        if (arg_types.empty()) {
            error(call.paren, "spawn() requires at least one argument — the function to execute in the new thread.");
            return;
        }

        auto closure_type = arg_types[0];
        if (closure_type->kind != TypeKind::FUNCTION) {
            error(call.paren, "The first argument to spawn() must be a function, but got type '" + closure_type->toString() + "'.");
            return;
        }
        auto func_type = std::dynamic_pointer_cast<FunctionType>(closure_type);

        size_t num_expected_args = func_type->param_types.size();
        size_t num_actual_args = arg_types.size() - 1;

        if (num_actual_args != num_expected_args) {
            error(call.paren, "Argument count mismatch in spawn(). The function expects " +
                              std::to_string(num_expected_args) + " argument(s), but " +
                              std::to_string(num_actual_args) + " were provided.");

            if (auto var_expr = std::dynamic_pointer_cast<const VarExpr>(call.arguments[0])) {
                if (auto symbol = m_symbols.resolve(var_expr->name.lexeme)) {
                    note(symbol->declaration_token, "Function '" + symbol->name + "' is defined here.");
                }
            }
            return;
        }

        for (size_t i = 0; i < num_actual_args; ++i) {
            const auto& expected_type = func_type->param_types[i];
            const auto& actual_type = arg_types[i + 1];

            if (!check_type_compatibility(expected_type, actual_type)) {
                error(call.paren, "Type mismatch for argument " + std::to_string(i + 1) + " of spawned function. " +
                                  "Expected '" + expected_type->toString() + "', but got '" + actual_type->toString() + "'.");

                if (auto var_expr = std::dynamic_pointer_cast<const VarExpr>(call.arguments[0])) {
                    if (auto symbol = m_symbols.resolve(var_expr->name.lexeme)) {
                        note(symbol->declaration_token, "Function '" + symbol->name + "' is defined here.");
                    }
                }
                return;
            }
        }
    }

    void TypeChecker::check_function_call(
            const CallExpr& call,
            const std::shared_ptr<FunctionType>& func_type,
            const std::vector<std::shared_ptr<Type>>& arg_types
    ) {
        // For foreign functions with callback userdata, skip hidden userdata params
        std::set<size_t> hidden_params(func_type->userdata_param_indices.begin(),
                                        func_type->userdata_param_indices.end());
        size_t num_visible = func_type->param_types.size() - hidden_params.size();

        // Build a mapping: visible_arg_index -> param_types_index
        std::vector<size_t> visible_to_param;
        for (size_t i = 0; i < func_type->param_types.size(); i++) {
            if (!hidden_params.count(i)) {
                visible_to_param.push_back(i);
            }
        }

        size_t num_expected = func_type->is_foreign ? num_visible : func_type->param_types.size();
        size_t num_actual = arg_types.size();
        bool arity_ok = true;

        if (func_type->is_variadic) {
            if (num_actual < num_expected) arity_ok = false;
        } else {
            if (num_actual > num_expected) {
                arity_ok = false;
            }
            else if (num_actual < num_expected) {
                for (size_t i = num_actual; i < num_expected; ++i) {
                    if (func_type->param_types[i]->kind != TypeKind::OPTIONAL) {
                        arity_ok = false;
                        break;
                    }
                }
            }
        }

        if (!arity_ok) {
            error(call.paren, "Incorrect number of arguments. Function expects " +
                              std::to_string(num_expected) + " argument(s), but got " +
                              std::to_string(num_actual) + ".");

            if (auto var_expr = std::dynamic_pointer_cast<const VarExpr>(call.callee)) {
                if (auto symbol = m_symbols.resolve(var_expr->name.lexeme)) {
                    note(symbol->declaration_token, "Function '" + symbol->name + "' is defined here.");
                }
            }
            return;
        }

        if (m_hadError) return;

        size_t check_limit = num_actual;

        if (func_type->is_variadic && check_limit > num_expected) {
            check_limit = num_expected;
        }

        for (size_t i = 0; i < check_limit; ++i) {
            // Map visible arg index to actual param index
            size_t param_idx = func_type->is_foreign && !visible_to_param.empty()
                               ? visible_to_param[i] : i;
            const auto& expected_type = func_type->param_types[param_idx];
            const auto& actual_type = arg_types[i];
            const auto& arg_expr = call.arguments[i];

            if (auto list_lit = std::dynamic_pointer_cast<const ListExpr>(arg_expr)) {
                if (list_lit->elements.empty()) {
                    if (expected_type->kind == TypeKind::LIST) {
                        continue;
                    }
                }
            }

            if (!check_type_compatibility(expected_type, actual_type)) {
                error(call.paren, "Type mismatch for argument " + std::to_string(i + 1) + ". " +
                                  "Expected '" + expected_type->toString() +
                                  "', but got '" + actual_type->toString() + "'.");
                return;
            }
        }
    }

}
