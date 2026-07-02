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

        std::shared_ptr<Type> return_type = m_type_nil;
        if (stmt.returnType) {
            return_type = resolveType(stmt.returnType);
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
        m_function_return_types.push(func_type->return_type);

        auto saved_type_params = m_active_type_params;
        for (const auto& tp : stmt->type_params) {
            m_active_type_params[tp.lexeme] = std::make_shared<TypeParameterType>(tp.lexeme);
        }

        if (stmt->has_this && m_current_class) {
            Token this_token(TokenType::THIS, "this", stmt->name.line, 0);
            m_symbols.declare(this_token, std::make_shared<InstanceType>(m_current_class), true);
        }

        for (size_t i = 0; i < stmt->params.size(); ++i) {
            m_symbols.declare(stmt->params[i].name, func_type->param_types[i], true);
        }

        for (const auto& bodyStmt : (*stmt->body)) {
            bodyStmt->accept(*this, bodyStmt);
        }

        // TS-6: definite-return check. If the function declares a non-nil return
        // type, every control-flow path must end in a `return` (or `throw`).
        // Skip when this function already reported an error (avoid cascades).
        if (!m_hadError && func_type->return_type &&
            func_type->return_type->kind != TypeKind::NIL &&
            func_type->return_type->kind != TypeKind::VOID) {
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

        m_active_type_params = saved_type_params;
        m_function_return_types.pop();
        exitScopeAndWarn();

        // Restore: if this function had errors, propagate to outer state
        if (m_hadError) saved_had_error = true;
        m_hadError = saved_had_error;
    }

}
