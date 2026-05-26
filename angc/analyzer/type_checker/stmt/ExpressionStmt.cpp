#include "TypeChecker.h"
namespace angara {

    void TypeChecker::visit(std::shared_ptr<const ExpressionStmt> stmt) {
        stmt->expression->accept(*this);
        popType();
    }

}
