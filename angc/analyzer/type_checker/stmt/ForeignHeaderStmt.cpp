//
// Created by cv2 on 27.09.2025.
//
#include "TypeChecker.h"

namespace angara {

    void TypeChecker::visit(std::shared_ptr<const ForeignHeaderStmt> stmt) {
        // This statement has no semantic content to check.
    }

}