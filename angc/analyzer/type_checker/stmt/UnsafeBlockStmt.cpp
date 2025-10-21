//
// Created by cv2 on 16.10.2025.
//
#include "TypeChecker.h"

namespace angara {

    void TypeChecker::visit(std::shared_ptr<const UnsafeBlockStmt> stmt) {
        // --- THIS IS THE CORE OF THE FEATURE ---

        // 1. Set the unsafe context flag to true.
        m_is_in_unsafe_context = true;

        // 2. Visit all the statements *inside* the block.
        //    Any expressions evaluated within this block will now know
        //    they are in an unsafe context.
        stmt->block->accept(*this, stmt->block);

        // 3. Crucially, reset the flag to false after we leave the block.
        m_is_in_unsafe_context = false;
    }

} // namespace angara