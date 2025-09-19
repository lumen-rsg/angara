//
// Created by cv2 on 9/19/25.
//

#include "TypeChecker.h"
namespace angara {

    void TypeChecker::visit(std::shared_ptr<const BlockStmt> stmt) {
        // 1. Enter a new lexical scope.
        m_symbols.enterScope();

        // 2. Type check every statement inside the block.
        for (const auto& statement : stmt->statements) {
            if (statement) {
                statement->accept(*this, statement);
            }
        }

        // 3. Exit the lexical scope, destroying all variables declared within it.
        m_symbols.exitScope();
    }

}