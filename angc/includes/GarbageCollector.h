#pragma once

// Forward declarations only — do NOT include LLVM headers here.
// Concrete GC implementations include their own LLVM headers.

namespace llvm {
class LLVMContext;
class Module;
class StructType;
class FunctionCallee;
class GlobalVariable;
class Value;
class Type;
class Function;
class BasicBlock;
class Constant;
}

namespace angara {

/// Abstract interface for garbage collector implementations.
/// Each implementation generates different LLVM IR for the same logical
/// memory management operations. The compiler's codegen calls through this
/// interface, allowing different GC strategies to be swapped at compile time.
class GarbageCollector {
public:
    virtual ~GarbageCollector() = default;

    virtual const char* name() const = 0;

    virtual void generateTypes() = 0;
    virtual void generateGlobals() = 0;
    virtual void generateFunctions() = 0;

    virtual llvm::StructType* getHeaderType() const = 0;
    virtual unsigned headerTypeIndex() const = 0;
    virtual unsigned headerMetaIndex() const = 0;
    virtual int headerNextIndex() const = 0;

    virtual llvm::FunctionCallee getAllocFunc() const = 0;
    virtual llvm::FunctionCallee getStoreTrackFunc() const = 0;
    virtual llvm::FunctionCallee getPushFrameFunc() const = 0;
    virtual llvm::FunctionCallee getPopFrameFunc() const = 0;
    virtual llvm::FunctionCallee getThreadRegisterFunc() const = 0;
    virtual llvm::FunctionCallee getThreadUnregisterFunc() const = 0;
    virtual llvm::FunctionCallee getGcPinFunc() const = 0;
    virtual llvm::FunctionCallee getGcUnpinFunc() const = 0;
    virtual llvm::FunctionCallee getGcPrintStatsFunc() const = 0;

    virtual void generateFreestandingStubs() = 0;

    // --- Stage 5: root frame / thread state accessors ---

    virtual llvm::StructType* getGcRootFrameType() const = 0;
    virtual llvm::StructType* getGcThreadStateType() const = 0;
    virtual llvm::GlobalVariable* getGcThreadStateTLS() const = 0;
};

} // namespace angara
