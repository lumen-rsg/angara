#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const LambdaExpr& expr) {
        std::vector<std::shared_ptr<Type>> param_types;
        for (const auto& ast_type : expr.param_types) {
            if (ast_type) {
                param_types.push_back(resolveType(ast_type));
            } else {
                param_types.push_back(m_type_any);
            }
        }

        std::shared_ptr<Type> return_type = m_type_nil;
        if (expr.returnType) {
            return_type = resolveType(expr.returnType);
        }

        m_symbols.enterScope();

        for (size_t i = 0; i < expr.param_names.size(); ++i) {
            m_symbols.declare(expr.param_names[i], param_types[i], false);
        }

        m_function_return_types.push(return_type);

        for (const auto& stmt : expr.body) {
            stmt->accept(*this, stmt);
        }

        m_function_return_types.pop();

        exitScopeAndWarn();

        auto func_type = std::make_shared<FunctionType>(param_types, return_type);
        pushAndSave(&expr, func_type);

        return {};
    }

}
