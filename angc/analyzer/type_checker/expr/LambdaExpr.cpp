//
// Created by cv2 on 4/19/26.
//
#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const LambdaExpr& expr) {
        // --- Phase 1: Resolve parameter types ---
        std::vector<std::shared_ptr<Type>> param_types;
        for (const auto& ast_type : expr.param_types) {
            if (ast_type) {
                param_types.push_back(resolveType(ast_type));
            } else {
                param_types.push_back(m_type_any); // Untyped params default to 'any'
            }
        }

        // --- Phase 2: Resolve return type ---
        std::shared_ptr<Type> return_type = m_type_nil; // Default to nil if not annotated
        if (expr.returnType) {
            return_type = resolveType(expr.returnType);
        }

        // --- Phase 3: Enter a new scope and declare parameters ---
        m_symbols.enterScope();

        // Lambda parameters are local variables
        for (size_t i = 0; i < expr.param_names.size(); ++i) {
            m_symbols.declare(expr.param_names[i], param_types[i], false);
        }

        // --- Phase 4: Type check the body ---
        // Push the return type so return statements can validate against it
        m_function_return_types.push(return_type);

        for (const auto& stmt : expr.body) {
            stmt->accept(*this, stmt);
        }

        m_function_return_types.pop();

        // --- Phase 5: Exit the scope ---
        exitScopeAndWarn();

        // --- Phase 6: Build the function type and save it ---
        auto func_type = std::make_shared<FunctionType>(param_types, return_type);
        pushAndSave(&expr, func_type);

        return {};
    }

}