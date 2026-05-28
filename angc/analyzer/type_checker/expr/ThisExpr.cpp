#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const ThisExpr& expr) {
        if (m_current_class == nullptr) {
            error(expr.keyword, "Cannot use 'this' outside of a class method.", "E376");
            pushAndSave(&expr, m_type_error);
            return {};
        }

        pushAndSave(&expr, std::make_shared<InstanceType>(m_current_class));
        return {};
    }

}
