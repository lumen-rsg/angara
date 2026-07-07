#include "TypeChecker.h"
namespace angara {

    void TypeChecker::defineFunctionHeader(const FuncStmt& stmt) {
        auto saved_type_params = m_active_type_params;
        for (const auto& tp : stmt.type_params) {
            m_active_type_params[tp.lexeme] = std::make_shared<TypeParameterType>(tp.lexeme);
        }

        std::vector<std::shared_ptr<Type>> param_types;

        if (stmt.has_this) {
            if (m_current_class == nullptr) {
                error(stmt.name, "'this' can only be used in a method, not in a standalone function.", "E268");
            }
        }

        for (const auto& p : stmt.params) {
            if (p.type) {
                param_types.push_back(resolveType(p.type));
            } else {
                error(p.name, "Parameter '" + p.name.lexeme + "' is missing a type annotation.", "E269");
                param_types.push_back(m_type_error);
            }
        }

        // LANG-11: store parameter names and default expressions.
        if (!stmt.is_foreign && !stmt.is_intrinsic) {
            std::string default_key = qualifiedFunctionKey(stmt.name.lexeme);
            if (stmt.has_this && m_current_class) {
                default_key = qualifiedFunctionKey(m_current_class->name + "." + stmt.name.lexeme);
            }
            // Store parameter names unconditionally.
            {
                std::vector<std::string> names;
                names.reserve(stmt.params.size());
                for (const auto& p : stmt.params) names.push_back(p.name.lexeme);
                m_function_param_names[default_key] = std::move(names);
            }
            // Store raw default expressions (validated later in body check).
            bool has_any_default = false;
            for (const auto& p : stmt.params) {
                if (p.default_value) { has_any_default = true; break; }
            }
            if (has_any_default) {
                std::vector<std::shared_ptr<Expr>> defaults(stmt.params.size(), nullptr);
                for (size_t i = 0; i < stmt.params.size(); ++i) {
                    defaults[i] = stmt.params[i].default_value;
                }
                m_function_defaults[default_key] = std::move(defaults);
            }
        }

        // TS-1/C4: record this function's param-bounds (param index -> bound
        // TraitType) so call sites can box bounded args into trait objects.
        // M9: also record type_param_name -> bound TraitType so call sites
        // can verify that inferred concrete type args satisfy their bounds.
        {
            std::map<size_t, std::shared_ptr<TraitType>> bounds;
            std::map<std::string, std::shared_ptr<TraitType>> tp_bounds;
            for (size_t i = 0; i < stmt.params.size(); ++i) {
                if (stmt.params[i].type) {
                    if (auto st = std::dynamic_pointer_cast<const SimpleType>(stmt.params[i].type)) {
                        auto bit = stmt.type_param_bounds.find(st->name.lexeme);
                        if (bit != stmt.type_param_bounds.end()) {
                            if (auto bsym = m_symbols.resolve(bit->second.lexeme)) {
                                if (bsym->type && bsym->type->kind == TypeKind::TRAIT) {
                                    auto trait = std::dynamic_pointer_cast<TraitType>(bsym->type);
                                    bounds[i] = trait;
                                    tp_bounds[st->name.lexeme] = trait;
                                }
                            }
                        }
                    }
                }
            }
            if (!bounds.empty()) m_function_bounds[qualifiedFunctionKey(stmt.name.lexeme)] = std::move(bounds);
            if (!tp_bounds.empty()) m_function_type_param_bounds[qualifiedFunctionKey(stmt.name.lexeme)] = std::move(tp_bounds);
        }

        std::shared_ptr<Type> return_type = m_type_nil;
        if (stmt.returnType) {
            return_type = resolveType(stmt.returnType);
        }

        // LIB-4: async functions return Future<T> (or Future<nil> if no explicit return type).
        // Foreign/intrinsic functions cannot be async.
        if (stmt.is_async) {
            if (stmt.is_foreign) {
                error(stmt.name, "An 'async' function cannot be 'foreign' — foreign functions are synchronous C imports.", "E417");
            }
            if (stmt.is_intrinsic) {
                error(stmt.name, "An 'async' function cannot be 'intrinsic' — intrinsics are synchronous compiler builtins.", "E418");
            }
            return_type = std::make_shared<FutureType>(return_type);
        }

        bool has_variadic = false;
        for (const auto& p : stmt.params) {
            if (p.is_variadic) { has_variadic = true; break; }
        }
        auto function_type = std::make_shared<FunctionType>(param_types, return_type, has_variadic);

        if (stmt.is_foreign) {
            function_type->is_foreign = true;

            // Identify userdata *void params that pair with FUNCTION callback params.
            // Convention: the next *void param after a FUNCTION param is its userdata slot.
            // These are auto-filled by the FFI layer and hidden from callers.
            for (size_t i = 0; i < param_types.size(); i++) {
                if (param_types[i]->kind == TypeKind::FUNCTION) {
                    // RT-1: stamp @on_throw value onto the callback's FunctionType
                    // so the FFI trampoline knows what C value to return on throw.
                    if (stmt.on_throw_value) {
                        auto cb_ft = std::dynamic_pointer_cast<FunctionType>(param_types[i]);
                        if (cb_ft) cb_ft->on_throw_value = stmt.on_throw_value;
                    }
                    for (size_t j = i + 1; j < param_types.size(); j++) {
                        if (param_types[j]->kind == TypeKind::POINTER) {
                            function_type->userdata_param_indices.push_back(j);
                            break;
                        }
                    }
                }
            }
        }

        if (stmt.is_intrinsic) {
            function_type->is_intrinsic = true;
        }

        // H9: propagate @consumes / @escape annotations to the FunctionType
        // so they survive module boundaries (attach). The Chaperone uses
        // these to apply ownership transitions at call sites.
        function_type->consumes_params = stmt.consumes_params;
        function_type->escape_params = stmt.escape_params;

        if (stmt.is_foreign || stmt.is_intrinsic) {
            if (auto conflicting = m_symbols.declare(stmt.name, function_type, true)) {
                error(stmt.name, "Symbol '" + stmt.name.lexeme + "' is already declared.", "E270");
                note(conflicting->declaration_token, "Previous declaration was here.");
            }
            if (stmt.is_exported) {
                error(stmt.name, "A 'foreign' function is an import and cannot be exported.", "E271");
            }
            return;
        }

        if (auto conflicting_symbol = m_symbols.declare(stmt.name, function_type, true)) {
            error(stmt.name, "Symbol '" + stmt.name.lexeme + "' is already declared.", "E272");
            note(conflicting_symbol->declaration_token, "Previous declaration was here.");
        }

        if (stmt.is_exported || stmt.name.lexeme == "main") {
            if (m_current_class != nullptr) {
                error(stmt.name, "'export' can only be used on top-level declarations.", "E273");
            } else {
                m_module_type->exports[stmt.name.lexeme] = function_type;
            }
        }

        m_active_type_params = saved_type_params;
    }

    void TypeChecker::visit(std::shared_ptr<const FuncStmt> stmt) {
        if (!stmt->body || stmt->is_foreign) {
            return;
        }

        // Reset per-function error state so errors in one function
        // don't poison type checking in subsequent functions.
        bool saved_had_error = m_hadError;
        m_hadError = false;

        // LIB-4: track whether we're inside an async function (for await validation)
        bool saved_in_async = m_in_async_function;
        m_in_async_function = stmt->is_async;

        auto symbol = m_symbols.resolve(stmt->name.lexeme);
        std::shared_ptr<FunctionType> func_type;
        if (m_current_class && m_current_class->methods.count(stmt->name.lexeme)) {
            func_type = std::dynamic_pointer_cast<FunctionType>(m_current_class->methods.at(stmt->name.lexeme).type);
        } else if (symbol && symbol->type->kind == TypeKind::FUNCTION) {
            func_type = std::dynamic_pointer_cast<FunctionType>(symbol->type);
        } else {
            return;
        }

        m_symbols.enterScope();
        // LIB-4: for async functions, the body returns T but the function type is Future<T>.
        // Push the inner (user-visible) return type so return statements are checked against T,
        // not Future<T>. The codegen wraps the result in the future.
        if (stmt->is_async && func_type->return_type->kind == TypeKind::FUTURE) {
            auto future_type = std::dynamic_pointer_cast<FutureType>(func_type->return_type);
            m_function_return_types.push(future_type->inner_type);
        } else {
            m_function_return_types.push(func_type->return_type);
        }

        auto saved_type_params = m_active_type_params;
        auto saved_bounds = m_active_type_param_bounds;
        for (const auto& tp : stmt->type_params) {
            m_active_type_params[tp.lexeme] = std::make_shared<TypeParameterType>(tp.lexeme);
        }
        // TS-1/C4: resolve this generic fn's bounds (T -> TraitType) so the body
        // can resolve trait methods against them and codegen can dispatch.
        for (const auto& [pname, bound_token] : stmt->type_param_bounds) {
            auto bsym = m_symbols.resolve(bound_token.lexeme);
            if (bsym && bsym->type && bsym->type->kind == TypeKind::TRAIT) {
                m_active_type_param_bounds[pname] =
                    std::dynamic_pointer_cast<TraitType>(bsym->type);
            }
        }

        if (stmt->has_this && m_current_class) {
            Token this_token(TokenType::THIS, "this", stmt->name.line, 0);
            m_symbols.declare(this_token, std::make_shared<InstanceType>(m_current_class), true);
        }

        for (size_t i = 0; i < stmt->params.size(); ++i) {
            const auto& param = stmt->params[i];
            if (!param.destructure_names.empty()) {
                // LANG-10: destructured parameter — declare each name with its element type
                auto param_type = func_type->param_types[i];
                if (param_type->kind != TypeKind::TUPLE) {
                    error(param.name,
                        "Destructured parameter '" + param.name.lexeme +
                        "' requires a tuple type after 'as', but got '" +
                        param_type->toString() + "'.",
                        "E415");
                    continue;
                }
                auto tuple_type = std::dynamic_pointer_cast<TupleType>(param_type);
                if (tuple_type->element_types.size() != param.destructure_names.size()) {
                    error(param.name,
                        "Destructured parameter arity mismatch. The tuple type '" +
                        param_type->toString() + "' has " +
                        std::to_string(tuple_type->element_types.size()) +
                        " element(s), but " +
                        std::to_string(param.destructure_names.size()) +
                        " name(s) were given.",
                        "E416");
                    continue;
                }
                for (size_t j = 0; j < param.destructure_names.size(); ++j) {
                    m_symbols.declare(param.destructure_names[j], tuple_type->element_types[j], true);
                }
            } else {
                m_symbols.declare(param.name, func_type->param_types[i], true);
            }
        }

        // LANG-11: validate default argument types.
        {
            std::string default_key = qualifiedFunctionKey(stmt->name.lexeme);
            if (stmt->has_this && m_current_class) {
                default_key = qualifiedFunctionKey(m_current_class->name + "." + stmt->name.lexeme);
            }
            auto def_it = m_function_defaults.find(default_key);
            if (def_it != m_function_defaults.end()) {
                for (size_t i = 0; i < stmt->params.size() && i < def_it->second.size(); ++i) {
                    if (def_it->second[i]) {
                        def_it->second[i]->accept(*this);
                        auto default_type = popType();
                        if (!m_hadError && default_type &&
                            default_type->kind != TypeKind::ERROR &&
                            i < func_type->param_types.size()) {
                            if (!check_type_compatibility(func_type->param_types[i], default_type,
                                    std::dynamic_pointer_cast<const Literal>(def_it->second[i]).get())) {
                                error(stmt->params[i].name,
                                      "Default value type mismatch for parameter '" +
                                      stmt->params[i].name.lexeme + "'. Expected '" +
                                      displayType(*func_type->param_types[i], *default_type) +
                                      "', but got '" + displayType(*default_type, *func_type->param_types[i]) + "'.",
                                      "E410");
                            }
                        }
                    }
                }
            }
        }

        for (const auto& bodyStmt : (*stmt->body)) {
            bodyStmt->accept(*this, bodyStmt);
        }

        // M8: store generic function ASTs for later body re-checking at call
        // sites. Only store if the body type-checked without errors — if the
        // initial check already failed there's no need to re-check.
        if (!stmt->type_params.empty() && !m_hadError) {
            std::string key = stmt->name.lexeme;
            if (stmt->has_this && m_current_class) {
                key = m_current_class->name + "." + stmt->name.lexeme;
            }
            m_generic_func_stmts[key] = stmt;
        }

        // TS-6: definite-return check. If the function declares a non-nil return
        // type, every control-flow path must end in a `return` (or `throw`).
        // Skip when this function already reported an error (avoid cascades).
        // LIB-4: for async functions, unwrap Future<T> — Future<nil> is effectively nil.
        {
            auto effective_return = func_type->return_type;
            if (stmt->is_async && effective_return->kind == TypeKind::FUTURE) {
                effective_return = std::dynamic_pointer_cast<FutureType>(effective_return)->inner_type;
            }
            if (!m_hadError && effective_return &&
                effective_return->kind != TypeKind::NIL &&
                effective_return->kind != TypeKind::VOID) {
                bool body_definitely_returns = false;
                for (const auto& bodyStmt : (*stmt->body)) {
                    if (definitelyReturns(bodyStmt)) { body_definitely_returns = true; break; }
                }
                if (!body_definitely_returns) {
                    error(stmt->name, "Missing 'return' on some control-flow paths in function '" +
                                      stmt->name.lexeme + "' declared to return '" +
                                      func_type->return_type->toString() + "'.", "E387");
                }
            }
        }

        m_active_type_params = saved_type_params;
        m_active_type_param_bounds = saved_bounds;
        m_function_return_types.pop();
        exitScopeAndWarn();

        // Restore: if this function had errors, propagate to outer state
        if (m_hadError) saved_had_error = true;
        m_hadError = saved_had_error;
        m_in_async_function = saved_in_async;  // LIB-4
    }

}
