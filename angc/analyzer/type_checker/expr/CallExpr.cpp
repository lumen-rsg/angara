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
            // SIMD-5: vector constructors — vec2(...), vec3(...), vec4(...), vec8(...)
            const std::string& name = var_expr->name.lexeme;
            if (name == "vec2" || name == "vec3" || name == "vec4" || name == "vec8") {
                int expected_size = std::stoi(name.substr(3));
                std::vector<std::shared_ptr<Type>> arg_types;
                for (const auto& arg_expr : expr.arguments) {
                    arg_expr->accept(*this);
                    arg_types.push_back(popType());
                }
                if (m_hadError) { pushAndSave(&expr, m_type_error); return {}; }
                if (arg_types.size() != static_cast<size_t>(expected_size)) {
                    error(var_expr->name, name + "() expects exactly " +
                          std::to_string(expected_size) + " argument(s), but got " +
                          std::to_string(arg_types.size()) + ".", "E425");
                    pushAndSave(&expr, m_type_error);
                    return {};
                }
                // Infer element type from the first argument
                auto elem_type = arg_types[0];
                if (elem_type->kind != TypeKind::PRIMITIVE ||
                    (!isInteger(elem_type) && !isFloat(elem_type))) {
                    error(var_expr->name, "Vector element type must be a primitive numeric type "
                          "(i32, i64, f32, f64, etc.), but got '" +
                          elem_type->toString() + "' from first argument.", "E426");
                    pushAndSave(&expr, m_type_error);
                    return {};
                }
                // All arguments must be compatible with the element type
                for (size_t i = 1; i < arg_types.size(); ++i) {
                    if (!isNumeric(arg_types[i])) {
                        error(var_expr->name, "Argument " + std::to_string(i + 1) +
                              " must be numeric, but got '" + arg_types[i]->toString() + "'.", "E427");
                        pushAndSave(&expr, m_type_error);
                        return {};
                    }
                }
                // Use the first argument's type as the element type
                auto vec_type = std::make_shared<VectorType>(elem_type, expected_size);
                pushAndSave(&expr, vec_type);
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

            // LANG-11: resolve named arguments + fill defaults for this call.
            std::string callee_key;
            if (auto* ve = dynamic_cast<const VarExpr*>(expr.callee.get())) {
                callee_key = ve->name.lexeme;
            } else if (auto* ge = dynamic_cast<const GetExpr*>(expr.callee.get())) {
                auto oit = m_expression_types.find(ge->object.get());
                if (oit != m_expression_types.end() && oit->second &&
                    oit->second->kind == TypeKind::INSTANCE) {
                    auto inst = std::dynamic_pointer_cast<const InstanceType>(oit->second);
                    callee_key = inst->class_type->name + "." + ge->name.lexeme;
                } else {
                    callee_key = ge->name.lexeme;
                }
            }
            // M9: qualify with module name to prevent cross-module collisions
            if (!callee_key.empty()) callee_key = qualifiedFunctionKey(callee_key);
            const std::vector<std::shared_ptr<Expr>>* arg_exprs_ptr = nullptr;
            // LANG-11: check if any argument is actually named (has a label).
            bool has_named = false;
            for (const auto& n : expr.arg_names) {
                if (n.has_value()) { has_named = true; break; }
            }
            if (!callee_key.empty() &&
                (has_named || m_function_defaults.count(callee_key))) {
                auto resolved = resolveCallArgs(expr, callee_key,
                                                func_type->param_types.size());
                if (!resolved.empty()) {
                    std::vector<std::shared_ptr<Type>> padded_types;
                    bool all_resolved = true;
                    for (auto& re : resolved) {
                        if (re) { re->accept(*this); padded_types.push_back(popType()); }
                        else { all_resolved = false; break; }
                    }
                    if (!m_hadError && all_resolved &&
                        padded_types.size() == func_type->param_types.size()) {
                        arg_types = std::move(padded_types);
                        m_resolved_args[&expr] = std::move(resolved);
                        auto it = m_resolved_args.find(&expr);
                        if (it != m_resolved_args.end()) arg_exprs_ptr = &it->second;
                    }
                }
            }

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

                    // M8: validate that substituted RawArray types have valid
                    // (primitive) element types. TYPE_PARAM elements are deferred
                    // in resolveType() but never re-checked after substitution.
                    {
                        bool raw_valid = true;
                        for (const auto& pt : sub_params) {
                            if (!validate_substituted_type(pt, expr.paren))
                                raw_valid = false;
                        }
                        if (!validate_substituted_type(sub_ret, expr.paren))
                            raw_valid = false;
                        if (!raw_valid) {
                            pushAndSave(&expr, m_type_error);
                            return {};
                        }
                    }

                    // M9: verify that each inferred concrete type arg satisfies its
                    // declared trait bound (e.g. render<T: Drawable>(42) should fail
                    // because i64 does not conform to Drawable). This mirrors the
                    // existing check for generic data/class/enum instantiation at
                    // TypeChecker.cpp:457-462.
                    auto tp_bounds_it = m_function_type_param_bounds.find(callee_key);
                    if (tp_bounds_it == m_function_type_param_bounds.end()) {
                        // M9: Try module::method as fallback for method calls.
                        auto dot_pos = callee_key.rfind('.');
                        if (dot_pos != std::string::npos) {
                            std::string module_prefix;
                            auto sep = callee_key.find("::");
                            if (sep != std::string::npos && sep < dot_pos)
                                module_prefix = callee_key.substr(0, sep + 2);
                            tp_bounds_it = m_function_type_param_bounds.find(
                                module_prefix + callee_key.substr(dot_pos + 1));
                        }
                    }
                    if (tp_bounds_it != m_function_type_param_bounds.end()) {
                        for (const auto& [tp_name, concrete_type] : inferred_args) {
                            auto bound_it = tp_bounds_it->second.find(tp_name);
                            if (bound_it != tp_bounds_it->second.end() &&
                                !conformsToTrait(concrete_type, bound_it->second)) {
                                error(expr.paren,
                                      "Type argument '" + concrete_type->toString() +
                                      "' does not satisfy the bound '" + tp_name + ": " +
                                      bound_it->second->toString() + "'.",
                                      "E391");
                            }
                        }
                    }
                }
                // TS-1/C4: if the callee has trait-bounded params, record which
                // args must be boxed into trait objects at the call site (so the
                // generic body receives a trait object and indirect dispatch
                // works without monomorphization). m_function_bounds carries the
                // resolved per-param bounds keyed by function name.
                if (auto var = std::dynamic_pointer_cast<const VarExpr>(expr.callee)) {
                    auto fbit = m_function_bounds.find(qualifiedFunctionKey(var->name.lexeme));
                    if (fbit != m_function_bounds.end()) {
                        std::vector<std::pair<size_t, std::shared_ptr<TraitType>>> boxed;
                        for (const auto& [pi, trait] : fbit->second) {
                            if (pi < expr.arguments.size()) boxed.push_back({pi, trait});
                        }
                        if (!boxed.empty()) m_generic_boxed_args[&expr] = std::move(boxed);
                    }
                }
            }

            // M8: full body re-check with concrete type arguments.
            // After substituting TYPE_PARAM → concrete types in the signature,
            // re-type-check the generic function body to catch any errors that
            // the TYPE_PARAM placeholders may have hidden.
            if (is_generic_fn && !inferred_args.empty() && !callee_key.empty()) {
                if (!recheck_generic_body(callee_key, inferred_args, expr.paren)) {
                    pushAndSave(&expr, m_type_error);
                    return {};
                }
            }

            check_function_call(expr, check_type, arg_types, arg_exprs_ptr);
            if (!m_hadError) {
                result_type = check_type->return_type;
                // LANG-8: if the return type is a generic enum, promote to
                // GenericInstanceType so the result carries its type arguments
                // (e.g., Result.Ok(42) returns Result<i64> not bare Result).
                if (result_type->kind == TypeKind::ENUM) {
                    auto et = std::dynamic_pointer_cast<EnumType>(result_type);
                    if (et->is_generic()) {
                        std::map<std::string, std::shared_ptr<Type>> enum_args;
                        for (const auto& tp_name : et->type_params) {
                            auto it = inferred_args.find(tp_name);
                            if (it != inferred_args.end()) {
                                enum_args[tp_name] = it->second;
                            } else {
                                // Uninferred param: keep as TypeParameterType (partial inference)
                                enum_args[tp_name] = std::make_shared<TypeParameterType>(tp_name);
                            }
                        }
                        result_type = std::make_shared<GenericInstanceType>(et, std::move(enum_args));
                    }
                }
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
            const std::vector<std::shared_ptr<Type>>& arg_types,
            const std::vector<std::shared_ptr<Expr>>* arg_exprs
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
            // LANG-11: use resolved arg expressions when provided (for default values).
            const auto& arg_expr = (arg_exprs && i < arg_exprs->size())
                                   ? (*arg_exprs)[i]
                                   : call.arguments[i];

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

    // LANG-11: resolve named arguments and fill in defaults for a function call.
    std::vector<std::shared_ptr<Expr>> TypeChecker::resolveCallArgs(
            const CallExpr& call,
            const std::string& callee_key,
            size_t param_count)
    {
        auto pn_it = m_function_param_names.find(callee_key);
        static const std::vector<std::string> empty_names;
        const auto& param_names = (pn_it != m_function_param_names.end())
                                  ? pn_it->second : empty_names;
        auto def_it = m_function_defaults.find(callee_key);
        static const std::vector<std::shared_ptr<Expr>> empty_defaults;
        const auto& defaults = (def_it != m_function_defaults.end())
                               ? def_it->second : empty_defaults;

        std::vector<std::shared_ptr<Expr>> resolved(param_count, nullptr);
        std::vector<bool> filled(param_count, false);

        // Pass 1: positional arguments.
        size_t arg_idx = 0;
        for (; arg_idx < call.arguments.size(); ++arg_idx) {
            if (arg_idx < call.arg_names.size() && call.arg_names[arg_idx].has_value()) {
                break;
            }
            if (arg_idx >= param_count) break;
            resolved[arg_idx] = call.arguments[arg_idx];
            filled[arg_idx] = true;
        }

        // Pass 2: named arguments.
        for (; arg_idx < call.arguments.size(); ++arg_idx) {
            if (arg_idx >= call.arg_names.size() || !call.arg_names[arg_idx].has_value()) {
                if (arg_idx < param_count) {
                    resolved[arg_idx] = call.arguments[arg_idx];
                    filled[arg_idx] = true;
                }
                continue;
            }
            const std::string& arg_name = call.arg_names[arg_idx]->lexeme;
            ssize_t param_idx = -1;
            for (size_t pi = 0; pi < param_names.size(); ++pi) {
                if (param_names[pi] == arg_name) { param_idx = (ssize_t)pi; break; }
            }
            if (param_idx < 0) {
                error(call.paren, "Named argument '" + arg_name +
                      "' does not match any parameter name.", "E411");
                return {};
            }
            if (filled[(size_t)param_idx]) {
                error(call.paren, "Duplicate argument for parameter '" + arg_name + "'.", "E412");
                return {};
            }
            resolved[(size_t)param_idx] = call.arguments[arg_idx];
            filled[(size_t)param_idx] = true;
        }

        // Pass 3: fill defaults.
        for (size_t pi = 0; pi < param_count; ++pi) {
            if (!filled[pi] && pi < defaults.size() && defaults[pi]) {
                resolved[pi] = defaults[pi];
                filled[pi] = true;
            }
        }

        return resolved;
    }

    // M8: recursively validate that RawArray types in a substituted type tree
    // have valid (primitive) element types. TYPE_PARAM elements are allowed
    // because they will be further substituted. Returns true if valid.
    bool TypeChecker::validate_substituted_type(
            const std::shared_ptr<Type>& type,
            const Token& token)
    {
        if (!type || m_hadError) return true;

        if (type->kind == TypeKind::RAW_ARRAY) {
            auto elem = std::dynamic_pointer_cast<RawArrayType>(type)->element_type;
            if (elem->kind != TypeKind::PRIMITIVE &&
                elem->kind != TypeKind::TYPE_PARAM) {
                error(token, "Raw array element type must be primitive, but got '" +
                      elem->toString() + "'. Use list<" + elem->toString() +
                      "> for complex types.", "E114");
                return false;
            }
            return validate_substituted_type(elem, token);
        }
        if (type->kind == TypeKind::LIST) {
            return validate_substituted_type(
                std::dynamic_pointer_cast<ListType>(type)->element_type, token);
        }
        if (type->kind == TypeKind::OPTIONAL) {
            return validate_substituted_type(
                std::dynamic_pointer_cast<OptionalType>(type)->wrapped_type, token);
        }
        if (type->kind == TypeKind::REF) {
            return validate_substituted_type(
                std::dynamic_pointer_cast<RefType>(type)->inner_type, token);
        }
        if (type->kind == TypeKind::FUTURE) {
            return validate_substituted_type(
                std::dynamic_pointer_cast<FutureType>(type)->inner_type, token);
        }
        if (type->kind == TypeKind::TUPLE) {
            for (const auto& e :
                 std::dynamic_pointer_cast<TupleType>(type)->element_types) {
                if (!validate_substituted_type(e, token)) return false;
            }
            return true;
        }
        if (type->kind == TypeKind::FUNCTION) {
            auto ft = std::dynamic_pointer_cast<FunctionType>(type);
            for (const auto& p : ft->param_types) {
                if (!validate_substituted_type(p, token)) return false;
            }
            return validate_substituted_type(ft->return_type, token);
        }
        return true;
    }

    // M8: re-check a generic function body with concrete type arguments.
    // This is called at each call site after type arg inference to verify
    // that the body remains type-correct with the concrete types (not just
    // the TYPE_PARAM placeholders used during the initial check).
    bool TypeChecker::recheck_generic_body(
            const std::string& func_name,
            const std::map<std::string, std::shared_ptr<Type>>& type_args,
            const Token& error_token)
    {
        (void)error_token; // reserved for future call-site error reporting
        auto stmt_it = m_generic_func_stmts.find(func_name);
        if (stmt_it == m_generic_func_stmts.end()) return true;
        const auto& stmt = stmt_it->second;
        if (!stmt->body || stmt->body->empty()) return true;

        // Save critical state for restoration after re-check
        auto saved_type_params = m_active_type_params;
        auto saved_bounds = m_active_type_param_bounds;
        bool saved_had_error = m_hadError;
        m_hadError = false;

        // Set concrete types as the active type parameters
        for (const auto& tp : stmt->type_params) {
            auto it = type_args.find(tp.lexeme);
            if (it != type_args.end()) {
                m_active_type_params[tp.lexeme] = it->second;
            }
        }
        // Clear bounds — trait conformance was already verified by M9
        m_active_type_param_bounds.clear();

        // Push a fresh scope and re-declare parameters with concrete types
        m_symbols.enterScope();

        // C4: any exception thrown below (e.g. from accept() during body
        // re-check) would previously skip the state restoration at the end,
        // leaving a leaked symbol-table scope, a corrupted return-type stack,
        // and m_active_type_params polluted with the call site's concrete
        // types. Wrap the setup + re-check in try/catch; the catch performs
        // the same restoration as the normal path, preserves the hadError
        // merge, then rethrows so the error is not silently dropped.
        try {
            // Build substituted function type for parameter declarations
            auto symbol = m_symbols.resolve(stmt->name.lexeme);
            std::shared_ptr<FunctionType> orig_func_type;
            if (symbol && symbol->type->kind == TypeKind::FUNCTION) {
                orig_func_type = std::dynamic_pointer_cast<FunctionType>(symbol->type);
            }

            // Re-declare 'this' if needed
            if (stmt->has_this && m_current_class) {
                Token this_token(TokenType::THIS, "this", stmt->name.line, 0);
                m_symbols.declare(this_token,
                    std::make_shared<InstanceType>(m_current_class), true);
            }

            // Push the return type for return-statement checking
            if (orig_func_type) {
                auto ret = substituteTypeArgs(orig_func_type->return_type, type_args);
                m_function_return_types.push(ret);
            } else {
                m_function_return_types.push(m_type_nil);
            }

            // Re-declare parameters with substituted concrete types
            for (size_t i = 0; i < stmt->params.size(); ++i) {
                const auto& param = stmt->params[i];
                std::shared_ptr<Type> param_type;
                if (orig_func_type && i < orig_func_type->param_types.size()) {
                    param_type = substituteTypeArgs(
                        orig_func_type->param_types[i], type_args);
                } else {
                    param_type = m_type_any;
                }
                if (!param.destructure_names.empty()) {
                    if (param_type->kind == TypeKind::TUPLE) {
                        auto tup = std::dynamic_pointer_cast<TupleType>(param_type);
                        for (size_t j = 0; j < param.destructure_names.size() &&
                                           j < tup->element_types.size(); ++j) {
                            m_symbols.declare(param.destructure_names[j],
                                             tup->element_types[j], true);
                        }
                    }
                } else {
                    m_symbols.declare(param.name, param_type, true);
                }
            }

            // Re-visit body statements with concrete types
            for (const auto& bodyStmt : (*stmt->body)) {
                bodyStmt->accept(*this, bodyStmt);
                if (m_hadError) break;
            }
        } catch (...) {
            // Restore the same state as the normal path so a thrown exception
            // doesn't leak scopes/stacks/params, then propagate the exception.
            m_function_return_types.pop();
            exitScopeAndWarn();
            m_active_type_params = saved_type_params;
            m_active_type_param_bounds = saved_bounds;
            if (m_hadError) saved_had_error = true;  // preserve a flagged error
            m_hadError = saved_had_error;
            throw;
        }

        // Restore state
        m_function_return_types.pop();
        exitScopeAndWarn();
        m_active_type_params = saved_type_params;
        m_active_type_param_bounds = saved_bounds;

        bool recheck_ok = !m_hadError;
        if (m_hadError) saved_had_error = true;
        m_hadError = saved_had_error;

        return recheck_ok;
    }

}
