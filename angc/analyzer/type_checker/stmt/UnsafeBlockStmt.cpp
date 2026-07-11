#include "TypeChecker.h"
namespace angara {

    // @unsafe block: opt out of the type system and borrow checker for inline
    // asm, pointer casts, etc. Save and restore the flag so a nested @unsafe
    // block does not clobber an enclosing @unsafe context on exit — otherwise
    // the outer block would silently lose its permissions for the statements
    // following the inner block (e.g. an `asm(...)` would spuriously hit E920).
    void TypeChecker::visit(std::shared_ptr<const UnsafeBlockStmt> stmt) {
        bool was_unsafe = m_is_in_unsafe_context;
        m_is_in_unsafe_context = true;
        stmt->block->accept(*this, stmt->block);
        m_is_in_unsafe_context = was_unsafe;
    }

}
