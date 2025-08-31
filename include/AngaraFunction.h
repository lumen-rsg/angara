
#pragma once

#include <string>
#include <memory>
#include <map>

// --- LLVM FORWARD DECLARATIONS ---
// forward-declare the LLVM types we need to avoid including heavy
// LLVM headers in this project's header files.
namespace llvm {
    class Function;
    class BasicBlock;
    class IRBuilderBase;
    class Value;
}

namespace angara {

/**
 * @class AngaraFunction
 * @brief Represents the state of a single function during LLVM code generation.
 *
 * This object holds a pointer to the llvm::Function being built, its entry
 * block, the IR builder, and a symbol table mapping local variable names to
 * their allocated memory locations (AllocaInsts) on the stack.
 */
    class AngaraFunction {
    public:
        // The llvm::Function this object corresponds to.
        llvm::Function* llvm_function;

        // The entry point for the function's code.
        llvm::BasicBlock* entry_block;

        // A symbol table mapping Angara variable names to their llvm::Value*
        // which represents their location in memory.
        std::map<std::string, llvm::Value*> named_values;

        // The arity (parameter count) of the function.
        int arity;

        // The name of the function.
        std::string name;

    public:
        AngaraFunction()
                : llvm_function(nullptr), entry_block(nullptr), arity(0) {}
    };

} // namespace angara

