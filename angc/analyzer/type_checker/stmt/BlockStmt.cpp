#include "TypeChecker.h"
namespace angara {

    void TypeChecker::visit(std::shared_ptr<const BlockStmt> stmt) {
        m_symbols.enterScope();

        for (const auto& statement : stmt->statements) {
            if (statement) {
                statement->accept(*this, statement);
            }
        }

        exitScopeAndWarn();
    }

}
