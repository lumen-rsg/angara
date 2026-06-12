#pragma once

#include "GarbageCollector.h"
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>

namespace angara {

/// Precise mark-and-sweep garbage collector with tri-color marking.
///
/// ObjHeader layout: { i32 type, i32 meta, i8* next }
///   - type:  OBJ_* subtype tag
///   - meta:  packed i32 with color (byte 0), is_unique (byte 1)
///   - next:  intrusive linked-list pointer for the allocation list
class MarkSweepGC : public GarbageCollector {
public:
    MarkSweepGC(llvm::LLVMContext& ctx, llvm::Module& module, llvm::IRBuilder<>& builder);

    const char* name() const override { return "mark-sweep"; }

    void generateTypes() override;
    void generateGlobals() override;
    void generateFunctions() override;

    llvm::StructType* getHeaderType() const override { return m_obj_header_type; }
    unsigned headerTypeIndex() const override { return 0; }
    unsigned headerMetaIndex() const override { return 1; }
    int headerNextIndex() const override { return 2; }

    llvm::FunctionCallee getAllocFunc() const override { return m_fn_gc_alloc; }
    llvm::FunctionCallee getStoreTrackFunc() const override { return m_fn_gc_clear_unique; }
    llvm::FunctionCallee getPushFrameFunc() const override { return m_fn_gc_push_frame; }
    llvm::FunctionCallee getPopFrameFunc() const override { return m_fn_gc_pop_frame; }
    llvm::FunctionCallee getThreadRegisterFunc() const override { return m_fn_gc_thread_register; }
    llvm::FunctionCallee getThreadUnregisterFunc() const override { return m_fn_gc_thread_unregister; }
    llvm::FunctionCallee getGcPinFunc() const override { return m_fn_gc_pin; }
    llvm::FunctionCallee getGcUnpinFunc() const override { return m_fn_gc_unpin; }
    llvm::FunctionCallee getGcPrintStatsFunc() const override { return m_fn_gc_print_stats; }

    void generateFreestandingStubs() override;

    llvm::StructType* getGcRootFrameType() const override { return m_gc_root_frame_type; }
    llvm::StructType* getGcThreadStateType() const override { return m_gc_thread_state_type; }
    llvm::GlobalVariable* getGcThreadStateTLS() const override { return m_g_gc_thread_state_tls; }

    // --- Constants ---
    static constexpr int COLOR_WHITE = 0;
    static constexpr int COLOR_GRAY  = 1;
    static constexpr int COLOR_BLACK = 2;
    static constexpr int UNIQUE_SHIFT = 8;

    static constexpr int packMeta(int color, bool is_unique) {
        return color | (is_unique ? (1 << UNIQUE_SHIFT) : 0);
    }

    // --- Additional accessors ---

    /// Set the AngaraObject struct type (called by RuntimeBuilder after generateTypes).
    void setAngaraObjType(llvm::StructType* ty) { m_angara_obj_type = ty; }

    /// Set all runtime struct types (called by RuntimeBuilder after generateTypes).
    /// Needed by the GC scanner for type-dispatched child traversal.
    void setStructTypes(
        llvm::StructType* string_type,
        llvm::StructType* list_type,
        llvm::StructType* record_type,
        llvm::StructType* record_entry_type,
        llvm::StructType* exception_type,
        llvm::StructType* closure_type,
        llvm::StructType* bound_method_type,
        llvm::StructType* thread_type,
        llvm::StructType* native_instance_type)
    {
        m_string_type = string_type;
        m_list_type = list_type;
        m_record_type = record_type;
        m_record_entry_type = record_entry_type;
        m_exception_type = exception_type;
        m_closure_type = closure_type;
        m_bound_method_type = bound_method_type;
        m_thread_type = thread_type;
        m_native_instance_type = native_instance_type;
    }

private:
    llvm::LLVMContext&  m_ctx;
    llvm::Module&       m_module;
    llvm::IRBuilder<>&  m_builder;

    // --- LLVM struct types ---
    llvm::StructType* m_angara_obj_type = nullptr;  // Set by RuntimeBuilder after generateTypes
    llvm::StructType* m_obj_header_type = nullptr;
    llvm::StructType* m_gc_thread_state_type = nullptr;
    llvm::StructType* m_gc_root_frame_type = nullptr;

    // --- Runtime struct types (set by RuntimeBuilder after generateTypes) ---
    llvm::StructType* m_string_type = nullptr;
    llvm::StructType* m_list_type = nullptr;
    llvm::StructType* m_record_type = nullptr;
    llvm::StructType* m_record_entry_type = nullptr;
    llvm::StructType* m_exception_type = nullptr;
    llvm::StructType* m_closure_type = nullptr;
    llvm::StructType* m_bound_method_type = nullptr;
    llvm::StructType* m_thread_type = nullptr;
    llvm::StructType* m_native_instance_type = nullptr;

    // --- LLVM globals ---
    llvm::GlobalVariable* m_g_gc_head = nullptr;
    llvm::GlobalVariable* m_g_gc_count = nullptr;
    llvm::GlobalVariable* m_g_gc_threshold = nullptr;
    llvm::GlobalVariable* m_g_gc_running = nullptr;
    llvm::GlobalVariable* m_g_gc_threads = nullptr;
    llvm::GlobalVariable* m_g_gc_thread_state_tls = nullptr;

    // --- Stats globals ---
    llvm::GlobalVariable* m_g_gc_collections = nullptr;
    llvm::GlobalVariable* m_g_gc_total_allocs = nullptr;
    llvm::GlobalVariable* m_g_gc_total_frees = nullptr;
    llvm::GlobalVariable* m_g_gc_total_bytes_alloc = nullptr;
    // --- Function callees ---
    llvm::FunctionCallee m_fn_gc_alloc;
    llvm::FunctionCallee m_fn_gc_collect;
    llvm::FunctionCallee m_fn_gc_mark_value;
    llvm::FunctionCallee m_fn_gc_mark;
    llvm::FunctionCallee m_fn_gc_scan;
    llvm::FunctionCallee m_fn_gc_sweep;
    llvm::FunctionCallee m_fn_gc_finalize;
    llvm::FunctionCallee m_fn_gc_mark_roots;
    llvm::FunctionCallee m_fn_gc_push_frame;
    llvm::FunctionCallee m_fn_gc_pop_frame;
    llvm::FunctionCallee m_fn_gc_thread_register;
    llvm::FunctionCallee m_fn_gc_thread_unregister;
    llvm::FunctionCallee m_fn_gc_safepoint;
    llvm::FunctionCallee m_fn_gc_clear_unique;
    llvm::FunctionCallee m_fn_gc_pin;
    llvm::FunctionCallee m_fn_gc_unpin;
    llvm::FunctionCallee m_fn_gc_print_stats;

    llvm::Function* createRuntimeFunc(const std::string& name, llvm::FunctionType* type);
};

} // namespace angara
