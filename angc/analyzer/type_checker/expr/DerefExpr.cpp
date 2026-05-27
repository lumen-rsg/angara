#include "TypeChecker.h"
namespace angara {

    std::any TypeChecker::visit(const DerefExpr& expr) {
        expr.right->accept(*this);
        auto ptr_type = popType();

        // Dereference is FFI-only: only allowed on pointer types
        if (ptr_type->kind != TypeKind::POINTER) {
            error(expr.op, "Cannot dereference non-pointer type '" +
                ptr_type->toString() + "'. Pointer dereference is only valid for FFI pointer types.");
            pushAndSave(&expr, m_type_error);
            return {};
        }

        auto pointer = std::dynamic_pointer_cast<PointerType>(ptr_type);
        pushAndSave(&expr, pointer->pointee_type);
        return {};
    }

}
