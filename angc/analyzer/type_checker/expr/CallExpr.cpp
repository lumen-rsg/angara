#include "TypeChecker.h"
#include <set>
namespace angara {

    std::any TypeChecker::visit(const CallExpr& expr) {
        // TS-2: intercept builtin free functions whose names are NOT user symbols
        // (hash) before accepting the callee, so they don't trigger E377.
        // `spawn` is also handled here for symmetry (it resolves as a function).
        if (auto var_expr = std::dynamic_pointer_cast<const VarExpr>(expr.callee)) {
            if (var_expr->name.lexeme == "hash") {
                std::vector<std::shared_ptr<Type>> arg_types;
                for (const auto& arg_expr : expr.arguments) {
                    arg_expr->accept(*this);
                    arg_types.push_back(popType());
                }
                if (m_hadError) { pushAndSave(&expr, m_type_error); return {}; }
                if (arg_types.size() != 1) {
                    error(var_expr->name, "hash() takes exactly one argument, but got " +
                                          std::to_string(arg_types.size()) + ".", "E392");
                    pushAndSave(&expr, m_type_error);
                    return {};
                }
                pushAndSave(&expr, m_type_i64);
                return {};
            }
        }

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

            // TS-2: generic function call — infer type args from the concrete
            // arguments, then substitute the param/return types so the call is
            // checked against a concrete signature (and the result type is a
            // concrete type, not a raw TypeParameterType). Non-generic functions
            // skip this and check directly.
            std::shared_ptr<FunctionType> check_type = func_type;
            std::map<std::string, std::shared_ptr<Type>> inferred_args;
            bool is_generic_fn = false;
            for (const auto& pt : func_type->param_types) {
                if (pt && pt->kind == TypeKind::TYPE_PARAM) { is_generic_fn = true; break; }
            }
            if (!is_generic_fn && func_type->return_type &&
                func_type->return_type->kind == TypeKind::TYPE_PARAM) {
                is_generic_fn = true;
            }

            if (is_generic_fn) {
                // Infer by matching each param pattern against its concrete arg.
                size_t n = std::min(func_type->param_types.size(), arg_types.size());
                for (size_t i = 0; i < n; ++i) {
                    extract_type_args(func_type->param_types[i], arg_types[i], inferred_args);
                }
                // Build a substituted signature to check/return against.
                if (!inferred_args.empty()) {
                    std::vector<std::shared_ptr<Type>> sub_params;
                    sub_params.reserve(func_type->param_types.size());
                    for (const auto& pt : func_type->param_types) {
                        sub_params.push_back(substituteTypeArgs(pt, inferred_args));
                    }
                    auto sub_ret = substituteTypeArgs(func_type->return_type, inferred_args);
                    check_type = std::make_shared<FunctionType>(sub_params, sub_ret, func_type->is_variadic);
                }
                // TS-1/C4: if the callee has trait-bounded params, record which
                // args must be boxed into trait objects at the call site (so the
                // generic body receives a trait object and indirect dispatch
                // works without monomorphization). m_function_bounds carries the
                // resolved per-param bounds keyed by function name.
                if (auto var = std::dynamic_pointer_cast<const VarExpr>(expr.callee)) {
                    auto fbit = m_function_bounds.find(var->name.lexeme);
                    if (fbit != m_function_bounds.end()) {
                        std::vector<std::pair<size_t, std::shared_ptr<TraitType>>> boxed;
                        for (const auto& [pi, trait] : fbit->second) {
                            if (pi < expr.arguments.size()) boxed.push_back({pi, trait});
                        }
                        if (!boxed.empty()) m_generic_boxed_args[&expr] = std::move(boxed);
                    }
                }
            }

            check_function_call(expr, check_type, arg_types);
            if (!m_hadError) {
                result_type = check_type->return_type;
            }
        }
        else if (callee_type->kind == TypeKind::CLASS) {
            auto class_type = std::dynamic_pointer_cast<ClassType>(callee_type);
            const auto* init_prop = class_type->findProperty("init");

            if (!init_prop) {
                if (!arg_types.empty()) {
                    error(expr.paren, "Class '" + class_type->name + "' has no constructor that accepts arguments.", "E324");
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
            // TS-2: for a generic data constructor, infer the type args from the
            // concrete arguments FIRST, substitute the constructor signature, then
            // check the substituted (concrete) signature. (Previously the raw
            // TypeParameterType-bearing signature was checked, which only passed
            // because TYPE_PARAM was compatible with anything.)
            std::shared_ptr<FunctionType> ctor_check = data_type->constructor_type;
            std::map<std::string, std::shared_ptr<Type>> inferred_args;
            if (data_type->is_generic()) {
                const auto& ctor_params = data_type->constructor_type->param_types;
                for (size_t i = 0; i < std::min(ctor_params.size(), arg_types.size()); ++i) {
                    extract_type_args(ctor_params[i], arg_types[i], inferred_args);
                }
                if (!inferred_args.empty()) {
                    std::vector<std::shared_ptr<Type>> sub_params;
                    sub_params.reserve(ctor_params.size());
                    for (const auto& pt : ctor_params) {
                        sub_params.push_back(substituteTypeArgs(pt, inferred_args));
                    }
                    ctor_check = std::make_shared<FunctionType>(
                        sub_params, data_type->constructor_type->return_type,
                        data_type->constructor_type->is_variadic);
                }
            }
            check_function_call(expr, ctor_check, arg_types);
            if (!m_hadError) {
                if (data_type->is_generic()) {
                    result_type = std::make_shared<GenericInstanceType>(data_type, std::move(inferred_args));
                } else {
                    result_type = data_type;
                }
            }
        }
        else if (callee_type->kind == TypeKind::ANY) {
            if (m_is_in_unsafe_context) {
                result_type = m_type_any;
            } else {
                error(expr.paren, "Cannot call a value of type 'any' — this requires an '@unsafe' block.", "E325");
            }
        }
        else {
            error(expr.paren, "Expression of type '" + callee_type->toString() + "' is not callable. Only functions, classes, and data types can be called.", "E326");
        }

        pushAndSave(&expr, result_type);
        return {};
    }

    void TypeChecker::check_spawn_call(
            const CallExpr& call,
            const std::vector<std::shared_ptr<Type>>& arg_types
    ) {
        if (arg_types.empty()) {
            error(call.paren, "spawn() requires at least one argument — the function to execute in the new thread.", "E327");
            return;
        }

        auto closure_type = arg_types[0];
        if (closure_type->kind != TypeKind::FUNCTION) {
            error(call.paren, "The first argument to spawn() must be a function, but got type '" + closure_type->toString() + "'.", "E328");
            return;
        }
        auto func_type = std::dynamic_pointer_cast<FunctionType>(closure_type);

        size_t num_expected_args = func_type->param_types.size();
        size_t num_actual_args = arg_types.size() - 1;

        if (num_actual_args != num_expected_args) {
            error(call.paren, "Argument count mismatch in spawn(). The function expects " +
                              std::to_string(num_expected_args) + " argument(s), but " +
                              std::to_string(num_actual_args) + " were provided.", "E329");

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

            if (!check_type_compatibility(expected_type, actual_type,
                    std::dynamic_pointer_cast<const Literal>(call.arguments[i + 1]).get())) {
                error(call.paren, "Type mismatch for argument " + std::to_string(i + 1) + " of spawned function. " +
                                  "Expected '" + expected_type->toString() + "', but got '" + actual_type->toString() + "'.", "E330");

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
                              std::to_string(num_actual) + ".", "E331");

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

            if (!check_type_compatibility(expected_type, actual_type,
                    std::dynamic_pointer_cast<const Literal>(arg_expr).get())) {
                error(call.paren, "Type mismatch for argument " + std::to_string(i + 1) + ". " +
                                  "Expected '" + displayType(*expected_type, *actual_type) +
                                  "', but got '" + displayType(*actual_type, *expected_type) + "'.", "E332");
                return;
            }
        }
    }

}
