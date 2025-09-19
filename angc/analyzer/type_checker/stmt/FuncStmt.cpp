//
// Created by cv2 on 9/19/25.
//

#include "TypeChecker.h"
namespace angara {

    void TypeChecker::defineFunctionHeader(const FuncStmt& stmt) {
        std::vector<std::shared_ptr<Type>> param_types;

        if (stmt.has_this) {
            if (m_current_class == nullptr) {
                error(stmt.name, "Cannot use 'this' in a non-method function.");
            }
        }

        for (const auto& p : stmt.params) {
            if (p.type) {
                param_types.push_back(resolveType(p.type));
            } else {
                error(p.name, "Missing type annotation for parameter '" + p.name.lexeme + "'.");
                param_types.push_back(m_type_error);
            }
        }

        std::shared_ptr<Type> return_type = m_type_nil;
        if (stmt.returnType) {
            return_type = resolveType(stmt.returnType);
        }

        auto function_type = std::make_shared<FunctionType>(param_types, return_type);

        if (auto conflicting_symbol = m_symbols.declare(stmt.name, function_type, true)) {
            error(stmt.name, "re-declaration of symbol '" + stmt.name.lexeme + "'.");
            note(conflicting_symbol->declaration_token, "previous declaration was here.");
        }

        if (stmt.is_exported || stmt.name.lexeme == "main") {
            if (m_current_class != nullptr) {
                error(stmt.name, "'export' can only be used on top-level declarations.");
            } else {
                m_module_type->exports[stmt.name.lexeme] = function_type;
            }
        }
    }

    void TypeChecker::visit(std::shared_ptr<const FuncStmt> stmt) {
        // This visitor is called in Pass 2 to check the body of a function.
        // The signature was already processed in Pass 1.

        // A function with no body (a trait method) has no implementation to check.
        if (!stmt->body) {
            return;
        }

        // 1. Fetch the full FunctionType from the symbol table (created in Pass 1).
        auto symbol = m_symbols.resolve(stmt->name.lexeme);
        // Note: for methods, the name is not in the global scope. We need to look it up
        // in the current class context.
        std::shared_ptr<FunctionType> func_type;
        if (m_current_class && m_current_class->methods.count(stmt->name.lexeme)) {
            func_type = std::dynamic_pointer_cast<FunctionType>(m_current_class->methods.at(stmt->name.lexeme).type);
        } else if (symbol && symbol->type->kind == TypeKind::FUNCTION) {
            func_type = std::dynamic_pointer_cast<FunctionType>(symbol->type);
        } else {
            return; // Error was already reported in Pass 1
        }

        // 2. Enter a new scope for the function's body.
        m_symbols.enterScope();
        m_function_return_types.push(func_type->return_type);

        // 3. If it's a method, declare 'this'.
        if (stmt->has_this && m_current_class) {
            Token this_token(TokenType::THIS, "this", stmt->name.line, 0);
            m_symbols.declare(this_token, std::make_shared<InstanceType>(m_current_class), true);
        }

        // 4. Declare all parameters as local variables.
        for (size_t i = 0; i < stmt->params.size(); ++i) {
            m_symbols.declare(stmt->params[i].name, func_type->param_types[i], true);
        }

        // 5. Type-check every statement in the function's body.
        for (const auto& bodyStmt : (*stmt->body)) {
            bodyStmt->accept(*this, bodyStmt);
        }


        // 6. Restore the context.
        m_function_return_types.pop();
        m_symbols.exitScope();
    }

}