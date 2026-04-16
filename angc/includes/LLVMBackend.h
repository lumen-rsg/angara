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

    class LLVMBackend {
    public:
        LLVMBackend(TypeChecker& type_checker, ErrorHandler& errorHandler,
                    const std::string& target_triple = "");
        ~LLVMBackend(); // Releases LLVM objects (cleanup at process exit)

        bool generate(const std::vector<std::shared_ptr<Stmt>>& statements,
                      const std::shared_ptr<ModuleType>& module_type,
                      std::vector<std::string>& all_module_names);

        const std::string& get_object_file_path() const { return objPath; }
        const std::string& get_ir_file_path() const { return irPath; }

    private:
        // LLVM Core
        std::unique_ptr<llvm::LLVMContext> ctx;
        std::unique_ptr<llvm::Module> mod;
        std::unique_ptr<llvm::IRBuilder<>> builder;

        // Runtime Builder
        std::unique_ptr<RuntimeBuilder> rt;

        // AngaraObject LLVM type
        llvm::StructType* objType = nullptr;

        // --- Expression codegen (dynamic_cast dispatch) ---
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
        llvm::Value* callModuleFn(const std::string& mod, const std::string& fn,
                                   const std::vector<std::shared_ptr<Expr>>& args);

        // --- Statement codegen ---
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

        // --- Top-level declarations ---
        void codegenTopLevelDecls(const std::vector<std::shared_ptr<Stmt>>& statements);
        void codegenGlobalVarDecl(const VarDeclStmt& stmt);
        void codegenFunctionDecl(const FuncStmt& stmt, const std::string& module_name);
        void codegenClassDecl(const ClassStmt& stmt);
        void codegenDataDecl(const DataStmt& stmt);
        void codegenEnumDecl(const EnumStmt& stmt);
        void codegenMainFunction(const std::vector<std::shared_ptr<Stmt>>& statements,
                                 const std::string& module_name,
                                 const std::vector<std::string>& all_module_names);

        // --- Inline AngaraObject constructors ---
        llvm::Value* makeNil();
        llvm::Value* makeBool(bool val);
        llvm::Value* makeBool(llvm::Value* val);
        llvm::Value* makeI64(int64_t val);
        llvm::Value* makeI64(llvm::Value* val);
        llvm::Value* makeF64(double val);
        llvm::Value* makeF64(llvm::Value* val);
        llvm::Value* makeStr(const std::string& str);

        // --- Inline extractors ---
        llvm::Value* getI64(llvm::Value* obj);
        llvm::Value* getF64(llvm::Value* obj);
        llvm::Value* getBool(llvm::Value* obj);
        llvm::Value* getTag(llvm::Value* obj);
        llvm::Value* isTruthy(llvm::Value* obj);

        // --- Runtime call helper ---
        llvm::Value* callRt(llvm::FunctionCallee callee, const std::vector<llvm::Value*>& args);

        // --- Variable management ---
        llvm::AllocaInst* allocLocal(llvm::Function* fn, const std::string& name);
        llvm::Value* loadVar(const std::string& name);
        void storeVar(const std::string& name, llvm::Value* val);

        // --- Helpers ---
        std::string mangle(const std::string& module, const std::string& name);
        std::string mangleMethod(const std::string& class_name, const std::string& method);
        std::string sanitize(const std::string& name);

        // --- References ---
        TypeChecker& m_type_checker;
        ErrorHandler& m_errorHandler;

        // --- State ---
        llvm::Triple targetTriple;
        std::string moduleName;
        int loopDepth = 0;
        llvm::BasicBlock* loopExit = nullptr;

        // Named values in current scope
        std::map<std::string, llvm::AllocaInst*> namedVals;
        // Global variables
        std::map<std::string, llvm::GlobalVariable*> globals;

        // --- Class/method tracking ---
        // Maps class/data name → constructor function name
        std::map<std::string, std::string> constructorLookup;
        // Maps method name → mangled function name (for dispatch)
        std::map<std::string, std::string> methodLookup;

        // Output paths
        std::string objPath;
        std::string irPath;
    };

} // namespace angara