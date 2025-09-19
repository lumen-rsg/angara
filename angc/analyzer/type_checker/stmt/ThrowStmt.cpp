//
// Created by cv2 on 9/19/25.
//
#include "TypeChecker.h"
namespace angara {

    void TypeChecker::visit(std::shared_ptr<const ThrowStmt> stmt) {
        stmt->expression->accept(*this);
        auto thrown_type = popType();


        // Rule: You can only throw objects of type Exception.
        if (thrown_type->kind != TypeKind::EXCEPTION) {
            error(stmt->keyword, "Can only throw objects of type 'Exception', but got '" + thrown_type->toString() + "'.");
        }
    }

}