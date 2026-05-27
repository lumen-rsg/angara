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
#include <string>
#include <vector>
#include <map>
#include <set>

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
                    bool dump_ir = false);

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

        /// Returns the path to the emitted object file (.o).
        const std::string& get_object_file_path() const { return objPath; }

        /// Returns the path to the emitted unoptimized IR file (.ll).
        const std::string& get_ir_file_path() const { return irPath; }

    private:
        std::unique_ptr<llvm::LLVMContext> ctx;
        std::unique_ptr<llvm::Module> mod;
        std::unique_ptr<llvm::IRBuilder<>> builder;

        std::unique_ptr<RuntimeBuilder> rt;

        llvm::StructType* objType = nullptr;

        /// Expression codegen dispatch. Returns the computed AngaraObject value.
        llvm::Value* cg(const std::shared_ptr<Expr>& expr);
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
        llvm::Value* cgMatch(const MatchExpr& e);
        llvm::Value* cgRetype(const RetypeExpr& e);
        llvm::Value* cgLambda(const LambdaExpr& e);

        /// Calls a closure AngaraObject with the given arguments.
        llvm::Value* cgClosureCall(llvm::Value* callee, const std::vector<llvm::Value*>& args);

        /// Calls a function from a resolved module by name.
        llvm::Value* callModuleFn(const std::string& mod, const std::string& fn,
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

        /// Emits top-level declarations (globals, functions, classes, data, enums).
        void codegenTopLevelDecls(const std::vector<std::shared_ptr<Stmt>>& statements);
        void codegenGlobalVarDecl(const VarDeclStmt& stmt);
        void codegenFunctionDecl(const FuncStmt& stmt, const std::string& module_name);
        void codegenForeignFuncDecl(const FuncStmt& stmt);
        void codegenClassDecl(const ClassStmt& stmt);
        void codegenDataDecl(const DataStmt& stmt);
        void codegenEnumDecl(const EnumStmt& stmt);

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

        /// Creates an alloca for an AngaraObject local variable at the function entry.
        llvm::AllocaInst* allocLocal(llvm::Function* fn, const std::string& name);
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

        /// Produces a mangled function name: __ang_<module>_<name>.
        std::string mangle(const std::string& module, const std::string& name);
        /// Produces a mangled method name: __ang_<class>_<method>.
        std::string mangleMethod(const std::string& class_name, const std::string& method);
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
        std::map<std::string, llvm::GlobalVariable*> globals;

        std::map<std::string, std::string> constructorLookup;
        std::map<std::string, std::string> methodLookup;
        std::string m_current_superclass;

        bool m_freestanding = false;
        bool m_dump_ir = false;

        int m_lambda_counter = 0;

        std::string objPath;
        std::string irPath;
    };

} // namespace angara
