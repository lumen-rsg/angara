#include "TypeChecker.h"
namespace angara {

    // @privileged block: a stronger opt-in than @unsafe, required by the
    // privilege-transition intrinsics (eret/set_spsr/set_elr). Entering it
    // implies @unsafe too (so inline asm inside needs no nested @unsafe wrapper).
    void TypeChecker::visit(std::shared_ptr<const PrivilegedBlockStmt> stmt) {
        bool was_unsafe = m_is_in_unsafe_context;
        m_is_in_privileged_context = true;
        m_is_in_unsafe_context = true;
        stmt->block->accept(*this, stmt->block);
        m_is_in_privileged_context = false;
        m_is_in_unsafe_context = was_unsafe;
    }

}
