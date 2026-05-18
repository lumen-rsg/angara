//
// Angara LLVM Backend — Runtime IR Builder
// Generates all runtime functions as LLVM IR directly into the module.
// No external runtime library dependency.
//

#ifndef ANGARA_RUNTIME_BUILDER_H
#define ANGARA_RUNTIME_BUILDER_H

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constant.h>
#include <llvm/IR/GlobalVariable.h>

namespace angara {

/// Tag values for AngaraObject.type
static constexpr int TAG_NIL  = 0;
static constexpr int TAG_BOOL = 1;
static constexpr int TAG_I64  = 2;
static constexpr int TAG_F64  = 3;
static constexpr int TAG_OBJ  = 4;

/// ObjectType tags (stored in Object.type for heap objects)
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

class RuntimeBuilder {
public:
    RuntimeBuilder(llvm::LLVMContext& context, llvm::Module& module, llvm::IRBuilder<>& builder,
                   bool freestanding = false);

    /// Generate all runtime types and functions into the module.
    void generateRuntime();

    // --- Type accessors (available after generateRuntime) ---
    llvm::StructType* getAngaraObjType()  const { return m_angara_obj_type; }
    llvm::StructType* getHeaderType()     const { return m_obj_header_type; }
    llvm::StructType* getStringType()     const { return m_string_type; }
    llvm::StructType* getListType()       const { return m_list_type; }
    llvm::StructType* getRecordType()     const { return m_record_type; }
    llvm::StructType* getRecordEntryType() const { return m_record_entry_type; }
    llvm::StructType* getExceptionType()  const { return m_exception_type; }
    llvm::StructType* getClosureType()    const { return m_closure_type; }
    llvm::StructType* getThreadType()     const { return m_thread_type; }
    llvm::StructType* getMutexType()      const { return m_mutex_type; }
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

    // --- IO intrinsic functions ---
    llvm::FunctionCallee getFuncIOPrint()         const { return m_fn_io_print; }
    llvm::FunctionCallee getFuncIOPrintln()       const { return m_fn_io_println; }
    llvm::FunctionCallee getFuncIOWrite()         const { return m_fn_io_write; }
    llvm::FunctionCallee getFuncIOFlush()         const { return m_fn_io_flush; }
    llvm::FunctionCallee getFuncIOReadLine()      const { return m_fn_io_read_line; }
    llvm::FunctionCallee getFuncIOReadAll()       const { return m_fn_io_read_all; }

    // --- Exception globals ---
    llvm::GlobalVariable* getExceptionChain()  const { return m_g_exception_chain; }
    llvm::GlobalVariable* getCurrentException() const { return m_g_current_exception; }

    // --- Module API vtable ---
    llvm::GlobalVariable* getAPIVtable() const { return m_api_vtable; }
    llvm::StructType* getNativeInstanceType() const { return m_native_instance_type; }

private:
    // --- Type generation ---
    void generateTypes();

    // --- External function declarations (libc) ---
    void declareCLibFunctions();

    // --- Runtime function implementations ---
    void generateMemoryManagement();
    void generateStringOps();
    void generateListOps();
    void generateRecordOps();
    void generateConversions();
    void generateEquality();
    void generateDeepClone();
    void generateClosureOps();
    void generateExceptionOps();
    void generateThreadOps();
    void generateMiscOps();
    void generateIOOps();
    void generateFreestandingStubs();
    void generateModuleAPIVTable();

    // --- Helper: create a runtime function ---
    llvm::Function* createRuntimeFunc(
        const std::string& name,
        llvm::FunctionType* type,
        bool variadic = false
    );

    llvm::LLVMContext&  m_ctx;
    llvm::Module&       m_module;
    llvm::IRBuilder<>&  m_builder;

    // --- LLVM types ---
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

    // --- Runtime function callees ---
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

    // --- IO intrinsic callees ---
    llvm::FunctionCallee m_fn_io_print;
    llvm::FunctionCallee m_fn_io_println;
    llvm::FunctionCallee m_fn_io_write;
    llvm::FunctionCallee m_fn_io_flush;
    llvm::FunctionCallee m_fn_io_read_line;
    llvm::FunctionCallee m_fn_io_read_all;

    // --- Exception globals ---
    llvm::GlobalVariable* m_g_exception_chain = nullptr;
    llvm::GlobalVariable* m_g_current_exception = nullptr;

    // --- Module API vtable (for native module runtime init) ---
    llvm::GlobalVariable* m_api_vtable = nullptr;

    // --- Freestanding mode ---
    bool m_freestanding = false;
};

} // namespace angara

#endif // ANGARA_RUNTIME_BUILDER_H