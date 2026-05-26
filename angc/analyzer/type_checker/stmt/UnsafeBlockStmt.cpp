#include "TypeChecker.h"
namespace angara {

    void TypeChecker::visit(std::shared_ptr<const UnsafeBlockStmt> stmt) {
        m_is_in_unsafe_context = true;
        stmt->block->accept(*this, stmt->block);
        m_is_in_unsafe_context = false;
    }

}
