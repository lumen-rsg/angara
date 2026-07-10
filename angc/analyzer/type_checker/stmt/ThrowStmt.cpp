#include "TypeChecker.h"
namespace angara {

    void TypeChecker::visit(std::shared_ptr<const ThrowStmt> stmt) {
        if (m_is_in_kernel_mode) {
            error(stmt->keyword, "'throw' is not allowed in --kernel mode (exceptions require setjmp/longjmp/printf/exit, which the kernel does not provide). Use return codes or 'match' instead.", "E900");
            return;
        }
        if (m_is_in_freestanding_mode) {
            error(stmt->keyword, "'throw' is not available in --freestanding mode (exceptions require setjmp/longjmp/printf/exit, which bare metal does not provide). Use return codes or 'match' instead.", "E910");
            return;
        }
        stmt->expression->accept(*this);
        auto thrown_type = popType();

        if (thrown_type->kind != TypeKind::EXCEPTION) {
            error(stmt->keyword, "Can only throw objects of type 'Exception', but got '" + thrown_type->toString() + "'.", "E267");
        }
    }

}
