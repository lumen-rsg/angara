#pragma once

#include "Expr.h"
#include "Stmt.h"
#include "TypeChecker.h"
#include "ErrorHandler.h"
namespace angara { class RuntimeBuilder; }
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/TargetParser/Triple.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <optional>

namespace angara {

    /// LLVM IR backend for the Angara compiler.
    /// Generates LLVM IR from the typed AST, emits object files, and optionally
    /// writes unoptimized IR for debugging. All values are represented as
    /// AngaraObject (tagged union: { i32 tag, i64 payload }) throughout codegen.
    class LLVMBackend {
    public:
        /// Constructs the backend, initializes the LLVM module, target machine,
        /// and generates the runtime IR (types + functions).
        /// @param type_checker  Completed type checker with resolved types.
        /// @param errorHandler  Error handler for reporting codegen failures.
        /// @param target_triple LLVM target triple (empty = host default).
        /// @param freestanding  If true, skip libc dependencies.
        LLVMBackend(TypeChecker& type_checker, ErrorHandler& errorHandler,
                    const std::string& target_triple = "",
                    bool freestanding = false,
                    bool dump_ir = false,
                    bool debug = false,
                    bool emit_llvm = false);

        /// Releases LLVM objects (cleanup at process exit).
        ~LLVMBackend();

        /// Runs the full codegen pipeline: top-level declarations, main function,
        /// IR verification, optimization passes, and object file emission.
        /// @param statements        Root AST statements.
        /// @param module_type        Resolved module type (for naming).
        /// @param all_module_names   Names of all compiled modules (for cross-module calls).
        /// @return True if code generation and object emission succeeded.
        bool generate(const std::vector<std::shared_ptr<Stmt>>& statements,
                      const std::shared_ptr<ModuleType>& module_type,
                      std::vector<std::string>& all_module_names);

        /// Sets the output directory for object and IR files.
        /// When set, files are written to `<dir>/ang_<module>.o` instead of CWD.
        void set_output_dir(const std::string& dir) { m_output_dir = dir; }

        /// Returns the path to the emitted object file (.o).
        const std::string& get_object_file_path() const { return objPath; }

        /// Returns the path to the emitted unoptimized IR file (.ll).
        const std::string& get_ir_file_path() const { return irPath; }

        /// Generates LLVM IR only (no object file emission).
        /// Returns ownership of the module and context for use by JIT compilers.
        std::pair<std::unique_ptr<llvm::Module>, std::unique_ptr<llvm::LLVMContext>>
        generateIR(const std::vector<std::shared_ptr<Stmt>>& statements,
                   const std::shared_ptr<ModuleType>& module_type,
                   std::vector<std::string>& all_module_names);

    private:
        std::unique_ptr<llvm::LLVMContext> ctx;
        std::unique_ptr<llvm::Module> mod;
        std::unique_ptr<llvm::IRBuilder<>> builder;

        std::unique_ptr<RuntimeBuilder> rt;

        llvm::StructType* objType = nullptr;

        /// Expression codegen dispatch. Returns the computed AngaraObject value.
        llvm::Value* cg(const std::shared_ptr<Expr>& expr);
        // TS-1: if `dst_type` is a trait/contract and the source expression is a
        // concrete instance, box the value into a trait object (looking up the
        // right per-(class,interface) vtable). Otherwise return the value as-is.
        llvm::Value* maybeBoxTraitObject(llvm::Value* value, const Expr* src_expr,
                                         const std::shared_ptr<Type>& dst_type);
        llvm::Value* cgLiteral(const Literal& e);
        llvm::Value* cgBinary(const Binary& e);
        llvm::Value* cgUnary(const Unary& e);
        llvm::Value* cgAssign(const AssignExpr& e);
        llvm::Value* cgUpdate(const UpdateExpr& e);
        llvm::Value* cgCall(const CallExpr& e);
        llvm::Value* cgGet(const GetExpr& e);
        llvm::Value* cgList(const ListExpr& e);
        llvm::Value* cgLogical(const LogicalExpr& e);
        llvm::Value* cgSubscript(const SubscriptExpr& e);
        llvm::Value* cgRecord(const RecordExpr& e);
        llvm::Value* cgTernary(const TernaryExpr& e);
        llvm::Value* cgIs(const IsExpr& e);
        llvm::Value* cgCast(const CastExpr& e);
        llvm::Value* cgDeref(const DerefExpr& e);
        /// LANG-1: materializes a range as an eager list<i64>.
        llvm::Value* cgRange(const RangeExpr& e);
        /// LANG-3: lowers an interpolated string to a concat chain.
        llvm::Value* cgInterpString(const InterpStringExpr& e);
        /// LANG-10: lowers a tuple literal to a list (same runtime repr).
        llvm::Value* cgTuple(const TupleExpr& e);
        /// LIB-4: lowers an await expression (currently synchronous — extracts result).
        llvm::Value* cgAwait(const AwaitExpr& e);
        llvm::Value* cgMatch(const MatchExpr& e);
        llvm::Value* cgLambda(const LambdaExpr& e);

        /// Helper for cgMatch: generates guard check + body evaluation for a matched case.
        void cgBodyWithGuard(const MatchCase& c, llvm::Function* fn,
                             llvm::BasicBlock* mg, llvm::BasicBlock* next_bb,
                             std::vector<std::pair<llvm::BasicBlock*, llvm::Value*>>& inc);

        /// Calls a closure AngaraObject with the given arguments.
        llvm::Value* cgClosureCall(llvm::Value* callee, const std::vector<llvm::Value*>& args);

        /// Calls a function from a resolved module by name.
        llvm::Value* callModuleFn(const std::string& mod, const std::string& fn,
                                   const std::vector<std::shared_ptr<Expr>>& args,
                                   const std::vector<std::shared_ptr<Type>>* param_types = nullptr,
                                   const std::vector<std::pair<size_t, std::shared_ptr<TraitType>>>* boxed_idx = nullptr);

        /// Calls a variadic foreign C function directly, marshalling fixed and variadic args.
        llvm::Value* callVariadicForeignFn(const std::string& c_func_name,
                                            const std::shared_ptr<FunctionType>& func_type,
                                            const std::vector<std::shared_ptr<Expr>>& args);

        /// Statement codegen dispatch.
        void cgStmt(const std::shared_ptr<Stmt>& stmt);
        void cgVarDecl(const VarDeclStmt& s);
        void cgBlock(const BlockStmt& s);
        void cgIf(const IfStmt& s);
        void cgWhile(const WhileStmt& s);
        void cgFor(const ForStmt& s);
        void cgForIn(const ForInStmt& s);
        void cgReturn(const ReturnStmt& s);
        void cgThrow(const ThrowStmt& s);
        void cgTry(const TryStmt& s);
        void cgDrop(const DropStmt& s);

        /// Emits top-level declarations (globals, functions, classes, data, enums).
        void codegenTopLevelDecls(const std::vector<std::shared_ptr<Stmt>>& statements);
        void codegenGlobalVarDecl(const VarDeclStmt& stmt);
        void codegenFunctionDecl(const FuncStmt& stmt, const std::string& module_name);
        /// LIB-4: codegen for async functions — wrapper allocates frame, calls resume.
        void codegenAsyncFuncDecl(const FuncStmt& stmt, const std::string& module_name);
        /// LIB-4 Stage S: generate the resumable state machine (foo$resume).
        void codegenAsyncResumeFunc(const FuncStmt& stmt, const std::string& module_name,
                                    llvm::Function* wrapper_fn,
                                    llvm::StructType* frame_struct_ty,
                                    const std::vector<int>& param_field_idx,
                                    const std::vector<const AwaitExpr*>& await_states,
                                    const std::vector<std::pair<std::string, int>>& local_slots,
                                    const std::shared_ptr<FunctionType>& sem_fn_type);
        /// LIB-4: walk the AST to collect all AwaitExpr nodes and assign state numbers.
        void collectAwaitStates(const std::shared_ptr<Expr>& expr,
                                std::vector<const AwaitExpr*>& awaits);
        void collectAwaitStatesStmt(const std::shared_ptr<Stmt>& stmt,
                                     std::vector<const AwaitExpr*>& awaits);
        /// LIB-4 Stage S: walk the body to collect VarDeclStmt nodes for frame slots.
        void collectAsyncLocals(const std::shared_ptr<Stmt>& stmt,
                                std::vector<std::pair<std::string, int>>& locals,
                                int& next_slot);
        void collectAsyncLocalsExpr(const std::shared_ptr<Expr>& expr,
                                     std::vector<std::pair<std::string, int>>& locals,
                                     int& next_slot);
        void codegenForeignFuncDecl(const FuncStmt& stmt);
        void codegenClassDecl(const ClassStmt& stmt);
        void codegenDataDecl(const DataStmt& stmt);
        void codegenForeignDataDecl(const DataStmt& stmt);
        void codegenEnumDecl(const EnumStmt& stmt);

        /// Returns true if the function body contains any non-primitive values that need GC.
        bool functionNeedsGC(const FuncStmt& stmt);
        bool exprNeedsGC(const std::shared_ptr<Expr>& expr);
        bool stmtNeedsGC(const std::shared_ptr<Stmt>& stmt);

        /// Resolves a semantic type to its C-compatible LLVM type for FFI.
        llvm::Type* resolveCFieldType(const std::shared_ptr<Type>& type);
        /// Converts a raw C value to an AngaraObject.
        llvm::Value* marshalCToAngara(llvm::Value* c_val, const std::shared_ptr<Type>& type);
        /// Extracts a raw C value from an AngaraObject.
        llvm::Value* marshalAngaraToC(llvm::Value* obj, const std::shared_ptr<Type>& type);
        /// Generates code for accessing a field on a foreign data struct.
        llvm::Value* cgForeignFieldAccess(const GetExpr& e, std::shared_ptr<DataType> data_type);

        /// Declares external wrappers for native module exports.
        void codegenNativeModuleDecls(const std::vector<std::shared_ptr<Stmt>>& statements);

        /// Generates the C-compatible main() or _start entry point.
        void codegenMainFunction(const std::vector<std::shared_ptr<Stmt>>& statements,
                                 const std::string& module_name,
                                 const std::vector<std::string>& all_module_names);

        /// Constructs an AngaraObject with TAG_NIL.
        llvm::Value* makeNil();
        /// Constructs an AngaraObject with TAG_BOOL from a compile-time bool.
        llvm::Value* makeBool(bool val);
        /// Constructs an AngaraObject with TAG_BOOL from an LLVM i1 value.
        llvm::Value* makeBool(llvm::Value* val);
        /// Constructs an AngaraObject with TAG_I64 from a compile-time int64.
        llvm::Value* makeI64(int64_t val);
        /// Constructs an AngaraObject with TAG_I64 from an LLVM i64 value.
        llvm::Value* makeI64(llvm::Value* val);
        /// Constructs an AngaraObject with TAG_F64 from a compile-time double.
        llvm::Value* makeF64(double val);
        /// Constructs an AngaraObject with TAG_F64 from an LLVM double value.
        llvm::Value* makeF64(llvm::Value* val);
        /// Constructs an AngaraObject string from a compile-time C string literal.
        llvm::Value* makeStr(const std::string& str);
        void createStrlitInitFn();

        /// Extracts the i64 payload from an AngaraObject.
        llvm::Value* getI64(llvm::Value* obj);
        /// Extracts the f64 payload (bitcast from i64) from an AngaraObject.
        llvm::Value* getF64(llvm::Value* obj);
        /// Extracts the bool payload (trunc from i64 to i1) from an AngaraObject.
        llvm::Value* getBool(llvm::Value* obj);
        /// Extracts the tag field from an AngaraObject.
        llvm::Value* getTag(llvm::Value* obj);
        /// Tests whether an AngaraObject is truthy (non-nil, non-zero).
        llvm::Value* isTruthy(llvm::Value* obj);

        /// Calls a runtime function by callee handle.
        llvm::Value* callRt(llvm::FunctionCallee callee, const std::vector<llvm::Value*>& args);
        /// Calls a runtime function by name. Returns nil if the function is not found.
        llvm::Value* callRtByName(const std::string& name, const std::vector<llvm::Value*>& args);

        /// LANG-4: converts `src`'s value to a string, choosing the right runtime
        /// function from the static (compile-time) type: a `char`-typed operand
        /// routes through __ang_char_to_string (renders the glyph); everything
        /// else uses __ang_to_string. This is the type-aware entry point for all
        /// string-conversion sites (println/print args, `string()` builtin,
        /// interpolation holes, `+` with a string operand).
        llvm::Value* toStrTyped(const std::shared_ptr<Expr>& src);

        /// Creates an alloca for an AngaraObject local variable at the function entry.
        llvm::AllocaInst* allocLocal(llvm::Function* fn, const std::string& name);
        /// Type-aware overload: uses raw LLVM type for unboxable primitives, skips GC root.
        llvm::AllocaInst* allocLocal(llvm::Function* fn, const std::string& name,
                                      const std::shared_ptr<Type>& type);
        /// Loads a named variable from local scope or globals.
        llvm::Value* loadVar(const std::string& name);
        /// Stores a value to a named variable in local scope or globals.
        void storeVar(const std::string& name, llvm::Value* val);

        /// Truncates an i64 value to the bit width of the target integer type, then zero-extends back.
        llvm::Value* truncateForType(llvm::Value* val, const std::shared_ptr<Type>& type);
        /// Returns true if the type is an unsigned integer type (u8, u16, u32, u64).
        static bool isUnsignedIntType(const std::shared_ptr<Type>& type);
        /// Returns true if the type is a sized integer type (i8..i64, u8..u64).
        static bool isSizedIntType(const std::shared_ptr<Type>& type);
        /// Returns the bit width of a sized integer type (8, 16, 32, or 64).
        static int getIntBitWidth(const std::shared_ptr<Type>& type);

        // --- Unboxed primitive support ---
        /// Kind of local variable storage: boxed (AngaraObject) or raw LLVM primitive.
        /// RAW_PTR is a C pointer (string→char*, *T, *void) used for FFI marshalling.
        enum class LocalKind { BOXED, RAW_I1, RAW_I64, RAW_F64, RAW_PTR };

        /// RT-2: returns (cached) a DWARF DIType for a LocalKind.
        llvm::DIType* diTypeForLocalKind(LocalKind kind);
        /// RT-2: emit llvm.dbg.declare for a local variable at the current insert point.
        void emitDbgDeclare(llvm::AllocaInst* alloca, const std::string& name,
                            int line, int col, LocalKind kind);
        /// Returns true if a type can be stored as a raw LLVM primitive (not boxed).
        static bool isUnboxableType(const std::shared_ptr<Type>& type);
        /// Maps a semantic type to the appropriate LocalKind.
        static LocalKind localKindForType(const std::shared_ptr<Type>& type);
        /// FFI: the C-side LocalKind for a foreign-function param/return
        /// (maps string/pointers to RAW_PTR, unlike localKindForType).
        static LocalKind ffiKindForType(const std::shared_ptr<Type>& type);
        /// FFI: whether a type can be marshalled directly to C.
        static bool isFFIMarshallable(const std::shared_ptr<Type>& type);
        /// Returns the raw LLVM type for a given LocalKind.
        llvm::Type* llvmTypeForLocalKind(LocalKind kind);
        /// Boxes a raw LLVM value into an AngaraObject.
        llvm::Value* boxRaw(llvm::Value* raw, LocalKind kind);
        /// Unboxes an AngaraObject to a raw LLVM value.
        llvm::Value* unboxToRaw(llvm::Value* objVal, LocalKind kind);

        // --- SIMD-5: Vector codegen helpers ---
        /// Maps a VectorType to the corresponding LLVM fixed vector type (e.g., <4 x float>).
        llvm::Type* llvmTypeForVector(const VectorType& vec_type);
        /// Allocates a new AngaraVector heap object, stores the raw LLVM vector value,
        /// and returns the boxed AngaraObject (TAG_OBJ + payload pointer).
        llvm::Value* makeVector(llvm::Value* raw_vec, const VectorType& vec_type);
        /// Extracts the raw LLVM vector value from a boxed AngaraObject of VECTOR kind.
        llvm::Value* extractVector(llvm::Value* boxed_obj, const VectorType& vec_type);

        /// Produces a mangled function name: __ang_<module>_<name>.
        std::string mangle(const std::string& module, const std::string& name);
        /// Produces a mangled method name: __ang_<class>_<method>.
        std::string mangleMethod(const std::string& class_name, const std::string& method);
        /// LANG-13: resolves a method name to its mangled LLVM function for a
        /// given type (walks the class chain via methodLookup). Returns empty
        /// string if the type is not a class/instance or the method is not found.
        std::string resolveMethodForType(const std::shared_ptr<Type>& type,
                                         const std::string& method_name);
        /// Replaces non-alphanumeric characters with underscores.
        std::string sanitize(const std::string& name);

        TypeChecker& m_type_checker;
        ErrorHandler& m_errorHandler;

        llvm::Triple targetTriple;
        std::string moduleName;
        int loopDepth = 0;
        llvm::BasicBlock* loopExit = nullptr;
        llvm::BasicBlock* loopContinue = nullptr;

        std::map<std::string, llvm::AllocaInst*> namedVals;
        std::map<std::string, std::shared_ptr<Type>> namedTypes;
        std::map<std::string, LocalKind> namedKinds;
        std::map<std::string, llvm::GlobalVariable*> globals;

        // String literal intern cache: maps literal text -> module-level global
        std::map<std::string, llvm::GlobalVariable*> m_string_literal_cache;

        // Centralized string-literal initialization function.  All per-literal
        // init calls (string_from_c + gc_pin + gc_clear_unique) are emitted here
        // instead of in whichever function first references the literal during
        // compilation.  This avoids a load-before-init bug where a literal whose
        // init code lives in function B (compiled first) is loaded by function A
        // (executed first) and reads zeroinitializer (NIL).
        llvm::Function* m_strlit_init_fn = nullptr;

        std::map<std::string, std::string> constructorLookup;
        std::map<std::string, int> enumVariantIndex;  // "EnumName.VariantName" -> declaration order index
        std::map<std::string, std::string> methodLookup;
        // TS-1: per-(class,interface) vtable globals, keyed "ClassName->InterfaceName".
        // Each is a ConstantArray of function pointers (one per interface method, in
        // declaration order). Populated in codegenClassDecl; consumed by trait-object
        // boxing (Phase C) and indirect dispatch.
        std::map<std::string, llvm::GlobalVariable*> traitVtables;
        // TS-1: method-name -> slot index within an interface's vtable, keyed
        // "InterfaceName.methodName". Lets indirect dispatch GEP the right slot.
        std::map<std::string, int> traitMethodSlots;
        std::string m_current_superclass;

        // Inlined main: when set, cgReturn emits branch+store instead of ret
        llvm::AllocaInst* m_inlined_main_ret_alloca = nullptr;
        llvm::BasicBlock* m_inlined_main_cleanup_bb = nullptr;

        // Foreign data: maps data name -> LLVM struct type and semantic DataType
        std::map<std::string, llvm::StructType*> m_foreign_struct_types;
        std::map<std::string, std::shared_ptr<DataType>> m_foreign_data_types;
        // Field names in declaration order (matching C struct layout)
        std::map<std::string, std::vector<std::string>> m_foreign_field_order;

        // Variadic foreign functions: maps C function name -> semantic FunctionType
        std::map<std::string, std::shared_ptr<FunctionType>> m_variadic_foreign_funcs;

        // Raw (unboxed) function signatures: maps mangled name -> pair(param LocalKinds, return LocalKind)
        // If a function is in this map, it uses raw LLVM types instead of objType
        struct RawFuncInfo {
            std::vector<LocalKind> param_kinds;
            LocalKind return_kind;
            // Optional semantic param/return types for FFI marshalling (string→char*,
            // *T pointers). Populated for foreign funcs with marshalled params.
            std::vector<std::shared_ptr<Type>> param_types;
            std::shared_ptr<Type> return_type;
        };
        std::map<std::string, RawFuncInfo> m_raw_functions;

        // When inside a raw-signature function, holds the return kind (empty otherwise)
        std::optional<LocalKind> m_current_raw_return_kind;

        // LIB-4: async function codegen state
        bool m_in_async_function = false;
        llvm::Value* m_current_async_frame = nullptr;       // future frame (i8* from malloc)
        llvm::StructType* m_current_async_frame_type = nullptr;  // frame struct type
        llvm::Value* m_current_async_state_ptr = nullptr;   // pointer to state field (GEP)
        llvm::Value* m_current_async_result_ptr = nullptr;  // pointer to result field (GEP)
        llvm::Value* m_current_async_waker_fn_ptr = nullptr; // pointer to waker_fn field (GEP)
        llvm::Value* m_current_async_waker_ctx_ptr = nullptr; // pointer to waker_ctx field (GEP)

        // LIB-4 Stage 5: state machine suspension tracking
        int m_async_await_idx = 0;                          // current await index
        std::vector<llvm::BasicBlock*> m_async_await_cont_bbs;  // resume blocks (state N → BB)
        llvm::BasicBlock* m_async_suspend_bb = nullptr;     // suspend/return block
        llvm::BasicBlock* m_async_loop_bb = nullptr;        // loop dispatch block
        llvm::Type* m_async_state_ty = nullptr;             // i32 state type
        llvm::Function* m_current_async_resume_fn = nullptr; // resume function for waker self-ref
        llvm::SwitchInst* m_async_dispatch_switch = nullptr;  // dispatch switch for state machine

        // LIB-4 Stage S: frame-based local storage for async functions
        std::map<std::string, int> m_async_local_slots;     // var name → frame GEP index
        int m_async_waker_fn_field_idx = 3;                 // frame field index of waker_fn
        int m_async_waker_ctx_field_idx = 4;                // frame field index of waker_ctx
        int m_async_loop_field_idx = 5;                     // frame field index of loop

        // Callback context: set by marshalAngaraToC for FUNCTION params (heap-allocated closure)
        llvm::Value* m_pending_callback_context = nullptr;

        bool m_freestanding = false;
        bool m_dump_ir = false;
        bool m_emit_llvm = false;
        bool m_debug = false;

        // Debug info (DWARF) generation
        std::unique_ptr<llvm::DIBuilder> m_di_builder;
        llvm::DIFile* m_di_file = nullptr;
        llvm::DICompileUnit* m_di_cu = nullptr;
        std::map<std::string, llvm::DISubprogram*> m_di_functions;
        std::map<int, llvm::DIType*> m_di_types;  // RT-2: LocalKind -> DIType cache
        llvm::DIScope* m_di_scope = nullptr;  // current debug scope (function/subprogram)
        llvm::DIFile* getOrCreateDIFile(const std::string& filename);
        void setDebugLoc(const Token& tok);
        void setDebugLoc(int line, int col);

        int m_lambda_counter = 0;

        // GC root frame state

        // BUG-5: exception-frame leak. A `try` pushes a frame onto the global
        // exception chain; the pop (__ang_try_end) is only emitted on the
        // fall-through path, so a return/break/continue out of a try body leaves
        // a stale frame — a later throw longjmps into it (dead stack). We save
        // the chain pointer at function entry and restore it on every function
        // exit (emitGcPopFrame), and save/restore per-loop for break/continue.
        llvm::Value* m_exc_chain_save = nullptr;          // function-entry chain
        std::vector<llvm::Value*> m_exc_loop_chain_saves; // one per enclosing loop

        // TS-1: downward-flowing expected element type for list-literal boxing.
        // Set by cgVarDecl/cgAssign when the target is list<Trait>, consulted by
        // cgList to box each element into a trait object. Null = no expectation.
        std::shared_ptr<Type> m_expected_list_elem_type;

        // RT-3: tail-call signal. Set by cgReturn when the return value is a
        // bare call (tail position); cgCall reads it to setTailCallKind on the
        // emitted CallInst. nullopt = not in tail position.
        std::optional<llvm::CallInst::TailCallKind> m_pending_tail;

        // v5: Chaperone exception-unwind plan (ThrowStmt* → vars to auto-drop).

        // v5: Set of tracked type names (owned + class) for drop cascades.
        std::set<std::string> m_tracked_types;

        // v5: Built-in heap-allocated type names that need cascade finalization.
        // These are types whose values are TAG_OBJ and have interior buffers
        // (string, list, record, etc.) or are simply heap-allocated structs
        // that need freeing when a parent class/data is dropped.
        // Populated in the LLVMBackend constructor.
        std::set<std::string> m_heap_types;

        void emitGcPushFrame(llvm::Function* fn, int slot_count);
        void emitGcPopFrame();
        llvm::Value* emitGcThreadSetup();
        void emitGcTeardown(llvm::Value* state_ptr);

        std::string objPath;
        std::string irPath;
        std::string m_output_dir;  // TOOL-2: optional output directory for .o/.ll files
    };

} // namespace angara
