#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const IsExpr& expr) {
        expr.object->accept(*this);
        popType();

        resolveType(expr.type);

        pushAndSave(&expr, m_type_bool);
        return {};
    }

}
