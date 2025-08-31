//
// Created by cv2 on 8/30/25.
//

#include "AngaraClosure.h"

namespace angara {

    AngaraClosure::AngaraClosure(std::shared_ptr<AngaraFunction> function)
            : m_function(std::move(function)),
              m_native_fn(nullptr) {
        // For a script function, the arity is stored in the blueprint.
        if (m_function) {
            m_arity = m_function->arity;
        } else {
            m_arity = 0; // Should not happen
        }
    }

// Constructor for a closure that wraps a native C++ function.
    AngaraClosure::AngaraClosure(NativeFn function, int arity)
            : m_function(nullptr),
              m_native_fn(function),
              m_arity(arity) {}

} // namespace angara