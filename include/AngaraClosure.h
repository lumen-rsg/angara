// AngaraClosure.h (replaces Callable.h)

#pragma once

#include <memory>
#include <vector>
#include "Value.h"          // For AngaraObject
#include "AngaraFunction.h" // For the script function blueprint

namespace angara {

// The C++ signature for all native functions callable by Angara.
// It takes an argument count and a pointer to the first argument on the VM's stack.
    using NativeFn = AngaraObject(*)(int argCount, AngaraObject* args);

/**
 * @class AngaraClosure
 * @brief The unified runtime representation for all callable functions.
 *
 * This object can wrap either a compiled Angara function blueprint (AngaraFunction)
 * or a native C++ function pointer (NativeFn).
 */
    class AngaraClosure {
    public:
        // Constructor for a closure that wraps a script function.
        explicit AngaraClosure(std::shared_ptr<AngaraFunction> function);

        // Constructor for a closure that wraps a native C++ function.
        explicit AngaraClosure(NativeFn function, int arity);

        // Check which type of function this closure holds.
        bool isNative() const { return m_native_fn != nullptr; }

        // Getters for the specific function types.
        std::shared_ptr<AngaraFunction> getScriptFunction() const { return m_function; }
        NativeFn getNativeFunction() const { return m_native_fn; }
        int getArity() const { return m_arity; }

    private:
        // Only one of these two pointers will be valid.
        std::shared_ptr<AngaraFunction> m_function; // Is valid for script functions.
        NativeFn m_native_fn;                       // Is valid for native functions.

        int m_arity; // Arity is stored directly for both types.
    };

} // namespace angara