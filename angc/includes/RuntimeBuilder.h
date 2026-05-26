#pragma once

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constant.h>
#include <llvm/IR/GlobalVariable.h>

namespace angara {

/// Tag values for the AngaraObject.type discriminant field.
/// TAG_NIL through TAG_F64 are unboxed (payload stored inline).
/// TAG_OBJ indicates a heap-allocated object referenced by pointer.
static constexpr int TAG_NIL  = 0;
static constexpr int TAG_BOOL = 1;
static constexpr int TAG_I64  = 2;
static constexpr int TAG_F64  = 3;
static constexpr int TAG_OBJ  = 4;

/// Subtype tags stored in ObjHeader.type for heap-allocated objects.
static constexpr int OBJ_STRING          = 0;
static constexpr int OBJ_LIST            = 1;
static constexpr int OBJ_RECORD          = 2;
static constexpr int OBJ_EXCEPTION       = 3;
static constexpr int OBJ_THREAD          = 4;
static constexpr int OBJ_MUTEX           = 5;
static constexpr int OBJ_CLOSURE         = 6;
static constexpr int OBJ_CLASS           = 7;
static constexpr int OBJ_INSTANCE        = 8;
static constexpr int OBJ_NATIVE_INSTANCE = 9;
static constexpr int OBJ_DATA_INSTANCE   = 10;
static constexpr int OBJ_ENUM_INSTANCE   = 11;
static constexpr int OBJ_BOUND_METHOD    = 12;

/// Generates all runtime types and functions as LLVM IR directly into the module.
/// This eliminates the need for an external runtime library — the runtime is
/// embedded in each compiled binary.
class RuntimeBuilder {
public:
    /// Constructs the runtime builder.
    /// @param context      LLVM context for type creation.
    /// @param module       Target LLVM module to emit into.
    /// @param builder      IR builder for generating instructions.
    /// @param freestanding If true, generates stubs instead of libc-dependent implementations.
    RuntimeBuilder(llvm::LLVMContext& context, llvm::Module& module, llvm::IRBuilder<>& builder,
                   bool freestanding = false);

    /// Generates all runtime types, function declarations, and implementations.
    /// Must be called once before any codegen. In freestanding mode, generates
    /// minimal stubs and builtin replacements for libc functions.
    void generateRuntime();

    // --- Type accessors (available after generateRuntime) ---

    /// Returns the AngaraObject struct type { i32 tag, i64 payload }.
    llvm::StructType* getAngaraObjType()  const { return m_angara_obj_type; }
    /// Returns the ObjHeader struct type { i32 type, i64 ref_count }.
    llvm::StructType* getHeaderType()     const { return m_obj_header_type; }
    /// Returns the AngaraString struct type.
    llvm::StructType* getStringType()     const { return m_string_type; }
    /// Returns the AngaraList struct type.
    llvm::StructType* getListType()       const { return m_list_type; }
    /// Returns the AngaraRecord struct type.
    llvm::StructType* getRecordType()     const { return m_record_type; }
    /// Returns the RecordEntry struct type { i8* key, AngaraObject value }.
    llvm::StructType* getRecordEntryType() const { return m_record_entry_type; }
    /// Returns the AngaraException struct type.
    llvm::StructType* getExceptionType()  const { return m_exception_type; }
    /// Returns the AngaraClosure struct type.
    llvm::StructType* getClosureType()    const { return m_closure_type; }
    /// Returns the AngaraThread struct type.
    llvm::StructType* getThreadType()     const { return m_thread_type; }
    /// Returns the AngaraMutex struct type.
    llvm::StructType* getMutexType()      const { return m_mutex_type; }
    /// Returns the AngaraBoundMethod struct type.
    llvm::StructType* getBoundMethodType() const { return m_bound_method_type; }

    // --- Runtime function accessors ---

    llvm::FunctionCallee getFuncStringFromC()     const { return m_fn_string_from_c; }
    llvm::FunctionCallee getFuncStringConcat()    const { return m_fn_string_concat; }
    llvm::FunctionCallee getFuncStringRepeat()    const { return m_fn_string_repeat; }
    llvm::FunctionCallee getFuncToString()        const { return m_fn_to_string; }
    llvm::FunctionCallee getFuncListNew()         const { return m_fn_list_new; }
    llvm::FunctionCallee getFuncListNewWithElem() const { return m_fn_list_new_with_elements; }
    llvm::FunctionCallee getFuncListPush()        const { return m_fn_list_push; }
    llvm::FunctionCallee getFuncListGet()         const { return m_fn_list_get; }
    llvm::FunctionCallee getFuncListSet()         const { return m_fn_list_set; }
    llvm::FunctionCallee getFuncRecordNew()       const { return m_fn_record_new; }
    llvm::FunctionCallee getFuncRecordGet()       const { return m_fn_record_get; }
    llvm::FunctionCallee getFuncRecordSet()       const { return m_fn_record_set; }
    llvm::FunctionCallee getFuncIncref()          const { return m_fn_incref; }
    llvm::FunctionCallee getFuncDecref()          const { return m_fn_decref; }
    llvm::FunctionCallee getFuncEquals()          const { return m_fn_equals; }
    llvm::FunctionCallee getFuncDeepClone()       const { return m_fn_deep_clone; }
    llvm::FunctionCallee getFuncToI64()           const { return m_fn_to_i64; }
    llvm::FunctionCallee getFuncToF64()           const { return m_fn_to_f64; }
    llvm::FunctionCallee getFuncToBool()          const { return m_fn_to_bool; }
    llvm::FunctionCallee getFuncTypeof()          const { return m_fn_typeof; }
    llvm::FunctionCallee getFuncLen()             const { return m_fn_len; }
    llvm::FunctionCallee getFuncClosureNew()      const { return m_fn_closure_new; }
    llvm::FunctionCallee getFuncCall()            const { return m_fn_call; }
    llvm::FunctionCallee getFuncExceptionNew()    const { return m_fn_exception_new; }
    llvm::FunctionCallee getFuncExceptionGetMessage() const { return m_fn_exception_get_message; }
    llvm::FunctionCallee getFuncThrow()           const { return m_fn_throw; }
    llvm::FunctionCallee getFuncTryBegin()        const { return m_fn_try_begin; }
    llvm::FunctionCallee getFuncTryEnd()          const { return m_fn_try_end; }
    llvm::FunctionCallee getFuncBoundMethodNew()  const { return m_fn_bound_method_new; }
    llvm::FunctionCallee getFuncThreadSpawn()     const { return m_fn_thread_spawn; }
    llvm::FunctionCallee getFuncThreadJoin()      const { return m_fn_thread_join; }
    llvm::FunctionCallee getFuncMutexNew()        const { return m_fn_mutex_new; }
    llvm::FunctionCallee getFuncMutexLock()       const { return m_fn_mutex_lock; }
    llvm::FunctionCallee getFuncMutexUnlock()     const { return m_fn_mutex_unlock; }
    llvm::FunctionCallee getFuncPrintf()          const { return m_fn_printf; }

    // --- IO intrinsic function accessors ---

    llvm::FunctionCallee getFuncIOPrint()         const { return m_fn_io_print; }
    llvm::FunctionCallee getFuncIOPrintln()       const { return m_fn_io_println; }
    llvm::FunctionCallee getFuncIOWrite()         const { return m_fn_io_write; }
    llvm::FunctionCallee getFuncIOFlush()         const { return m_fn_io_flush; }
    llvm::FunctionCallee getFuncIOReadLine()      const { return m_fn_io_read_line; }
    llvm::FunctionCallee getFuncIOReadAll()       const { return m_fn_io_read_all; }

    // --- Exception globals ---

    /// Returns the global exception chain pointer (linked list of setjmp frames).
    llvm::GlobalVariable* getExceptionChain()  const { return m_g_exception_chain; }
    /// Returns the global variable holding the current caught exception.
    llvm::GlobalVariable* getCurrentException() const { return m_g_current_exception; }

    // --- Module API vtable ---

    /// Returns the AngaraAPI struct constant (function pointer table for native modules).
    llvm::GlobalVariable* getAPIVtable() const { return m_api_vtable; }
    /// Returns the AngaraNativeInstance struct type.
    llvm::StructType* getNativeInstanceType() const { return m_native_instance_type; }

private:
    /// Creates all LLVM struct types for the runtime object model.
    void generateTypes();

    /// Declares external libc functions (malloc, free, printf, pthreads, etc.).
    void declareCLibFunctions();

    /// Generates reference counting (incref/decref) and object deallocation.
    void generateMemoryManagement();
    /// Generates string allocation, concatenation, repetition, and to_string.
    void generateStringOps();
    /// Generates list allocation, push, get, and set operations.
    void generateListOps();
    /// Generates record allocation, get, and set operations.
    void generateRecordOps();
    /// Generates type conversion functions (to_i64, to_f64, to_bool, typeof).
    void generateConversions();
    /// Generates the equality comparison function.
    void generateEquality();
    /// Generates the deep clone function (currently a shallow incref clone).
    void generateDeepClone();
    /// Generates closure allocation and the generic __ang_call dispatcher.
    void generateClosureOps();
    /// Generates exception creation, throw (longjmp), and try/catch frame management.
    void generateExceptionOps();
    /// Generates thread spawn, join, mutex, and lock/unlock operations.
    void generateThreadOps();
    /// Generates misc functions: len() for strings and lists.
    void generateMiscOps();
    /// Generates IO functions: print, println, write, flush, read_line, read_all.
    void generateIOOps();
    /// Generates minimal stub functions for freestanding (bare-metal) targets.
    void generateFreestandingStubs();
    /// Builds the AngaraAPI vtable struct for native module interop.
    void generateModuleAPIVTable();

    /// Creates an internal-linkage runtime function in the module.
    /// @param name      Function name (e.g., "__ang_list_new").
    /// @param type      The LLVM function type.
    /// @param variadic  Whether the function is variadic.
    /// @return The created LLVM Function.
    llvm::Function* createRuntimeFunc(
        const std::string& name,
        llvm::FunctionType* type,
        bool variadic = false
    );

    llvm::LLVMContext&  m_ctx;
    llvm::Module&       m_module;
    llvm::IRBuilder<>&  m_builder;

    llvm::StructType* m_angara_obj_type = nullptr;
    llvm::StructType* m_obj_header_type = nullptr;
    llvm::StructType* m_string_type = nullptr;
    llvm::StructType* m_list_type = nullptr;
    llvm::StructType* m_record_type = nullptr;
    llvm::StructType* m_record_entry_type = nullptr;
    llvm::StructType* m_exception_type = nullptr;
    llvm::StructType* m_closure_type = nullptr;
    llvm::StructType* m_thread_type = nullptr;
    llvm::StructType* m_mutex_type = nullptr;
    llvm::StructType* m_bound_method_type = nullptr;
    llvm::StructType* m_native_instance_type = nullptr;

    llvm::FunctionCallee m_fn_string_from_c;
    llvm::FunctionCallee m_fn_string_concat;
    llvm::FunctionCallee m_fn_string_repeat;
    llvm::FunctionCallee m_fn_to_string;
    llvm::FunctionCallee m_fn_list_new;
    llvm::FunctionCallee m_fn_list_new_with_elements;
    llvm::FunctionCallee m_fn_list_push;
    llvm::FunctionCallee m_fn_list_get;
    llvm::FunctionCallee m_fn_list_set;
    llvm::FunctionCallee m_fn_record_new;
    llvm::FunctionCallee m_fn_record_get;
    llvm::FunctionCallee m_fn_record_set;
    llvm::FunctionCallee m_fn_incref;
    llvm::FunctionCallee m_fn_decref;
    llvm::FunctionCallee m_fn_equals;
    llvm::FunctionCallee m_fn_deep_clone;
    llvm::FunctionCallee m_fn_to_i64;
    llvm::FunctionCallee m_fn_to_f64;
    llvm::FunctionCallee m_fn_to_bool;
    llvm::FunctionCallee m_fn_typeof;
    llvm::FunctionCallee m_fn_len;
    llvm::FunctionCallee m_fn_closure_new;
    llvm::FunctionCallee m_fn_call;
    llvm::FunctionCallee m_fn_exception_new;
    llvm::FunctionCallee m_fn_exception_get_message;
    llvm::FunctionCallee m_fn_throw;
    llvm::FunctionCallee m_fn_try_begin;
    llvm::FunctionCallee m_fn_try_end;
    llvm::FunctionCallee m_fn_bound_method_new;
    llvm::FunctionCallee m_fn_thread_spawn;
    llvm::FunctionCallee m_fn_thread_join;
    llvm::FunctionCallee m_fn_mutex_new;
    llvm::FunctionCallee m_fn_mutex_lock;
    llvm::FunctionCallee m_fn_mutex_unlock;
    llvm::FunctionCallee m_fn_printf;

    llvm::FunctionCallee m_fn_io_print;
    llvm::FunctionCallee m_fn_io_println;
    llvm::FunctionCallee m_fn_io_write;
    llvm::FunctionCallee m_fn_io_flush;
    llvm::FunctionCallee m_fn_io_read_line;
    llvm::FunctionCallee m_fn_io_read_all;

    llvm::GlobalVariable* m_g_exception_chain = nullptr;
    llvm::GlobalVariable* m_g_current_exception = nullptr;

    llvm::GlobalVariable* m_api_vtable = nullptr;

    bool m_freestanding = false;
};

} // namespace angara
