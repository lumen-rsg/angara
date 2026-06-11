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

    void generateFreestandingStubs() override;

    // --- Constants ---
    static constexpr int COLOR_WHITE = 0;
    static constexpr int COLOR_GRAY  = 1;
    static constexpr int COLOR_BLACK = 2;
    static constexpr int UNIQUE_SHIFT = 8;

    static constexpr int packMeta(int color, bool is_unique) {
        return color | (is_unique ? (1 << UNIQUE_SHIFT) : 0);
    }

    // --- Additional accessors ---
    llvm::StructType* getGcThreadStateType() const { return m_gc_thread_state_type; }
    llvm::StructType* getGcRootFrameType() const { return m_gc_root_frame_type; }

    /// Set the AngaraObject struct type (called by RuntimeBuilder after generateTypes).
    void setAngaraObjType(llvm::StructType* ty) { m_angara_obj_type = ty; }

private:
    llvm::LLVMContext&  m_ctx;
    llvm::Module&       m_module;
    llvm::IRBuilder<>&  m_builder;

    // --- LLVM struct types ---
    llvm::StructType* m_angara_obj_type = nullptr;  // Set by RuntimeBuilder after generateTypes
    llvm::StructType* m_obj_header_type = nullptr;
    llvm::StructType* m_gc_thread_state_type = nullptr;
    llvm::StructType* m_gc_root_frame_type = nullptr;

    // --- LLVM globals ---
    llvm::GlobalVariable* m_g_gc_head = nullptr;
    llvm::GlobalVariable* m_g_gc_count = nullptr;
    llvm::GlobalVariable* m_g_gc_threshold = nullptr;
    llvm::GlobalVariable* m_g_gc_running = nullptr;
    llvm::GlobalVariable* m_g_gc_threads = nullptr;
    llvm::GlobalVariable* m_g_gc_thread_state_tls = nullptr;

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

    llvm::Function* createRuntimeFunc(const std::string& name, llvm::FunctionType* type);
};

} // namespace angara
