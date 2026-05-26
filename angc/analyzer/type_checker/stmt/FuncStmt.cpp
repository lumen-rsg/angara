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
                error(stmt.name, "'this' can only be used in a method, not in a standalone function.");
            }
        }

        for (const auto& p : stmt.params) {
            if (p.type) {
                param_types.push_back(resolveType(p.type));
            } else {
                error(p.name, "Parameter '" + p.name.lexeme + "' is missing a type annotation.");
                param_types.push_back(m_type_error);
            }
        }

        std::shared_ptr<Type> return_type = m_type_nil;
        if (stmt.returnType) {
            return_type = resolveType(stmt.returnType);
        }

        auto function_type = std::make_shared<FunctionType>(param_types, return_type);

        if (stmt.is_foreign) {
            function_type->is_foreign = true;
        }

        if (stmt.is_intrinsic) {
            function_type->is_intrinsic = true;
        }

        if (stmt.is_foreign || stmt.is_intrinsic) {
            if (auto conflicting = m_symbols.declare(stmt.name, function_type, true)) {
                error(stmt.name, "Symbol '" + stmt.name.lexeme + "' is already declared.");
                note(conflicting->declaration_token, "Previous declaration was here.");
            }
            if (stmt.is_exported) {
                error(stmt.name, "A 'foreign' function is an import and cannot be exported.");
            }
            return;
        }

        if (auto conflicting_symbol = m_symbols.declare(stmt.name, function_type, true)) {
            error(stmt.name, "Symbol '" + stmt.name.lexeme + "' is already declared.");
            note(conflicting_symbol->declaration_token, "Previous declaration was here.");
        }

        if (stmt.is_exported || stmt.name.lexeme == "main") {
            if (m_current_class != nullptr) {
                error(stmt.name, "'export' can only be used on top-level declarations.");
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

        m_active_type_params = saved_type_params;
        m_function_return_types.pop();
        exitScopeAndWarn();
    }

}
