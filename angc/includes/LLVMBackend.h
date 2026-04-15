#pragma once

#include "Expr.h"
#include "Stmt.h"
#include "TypeChecker.h"
#include "ErrorHandler.h"
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
        LLVMBackend(TypeChecker& type_checker, ErrorHandler& errorHandler);
        ~LLVMBackend() = default;

        // Main entry point
        bool generate(const std::vector<std::shared_ptr<Stmt>>& statements,
                      const std::shared_ptr<ModuleType>& module_type,
                      std::vector<std::string>& all_module_names);

        // Get output object file path
        const std::string& get_object_file_path() const { return m_object_file_path; }
        const std::string& get_ir_file_path() const { return m_ir_file_path; }

    private:
        // --- LLVM Core ---
        std::unique_ptr<llvm::LLVMContext> m_context;
        std::unique_ptr<llvm::Module> m_module;
        std::unique_ptr<llvm::IRBuilder<>> m_builder;

        // --- AngaraObject LLVM Type ---
        llvm::StructType* m_angara_obj_type;
        llvm::PointerType* m_angara_obj_ptr_type;
        llvm::FunctionType* m_generic_fn_type;

        // --- Expression Codegen ---
        llvm::Value* codegenExpr(const std::shared_ptr<Expr>& expr);
        llvm::Value* codegenLiteral(const Literal& expr);
        llvm::Value* codegenBinary(const Binary& expr);
        llvm::Value* codegenUnary(const Unary& expr);
        llvm::Value* codegenGrouping(const Grouping& expr);
        llvm::Value* codegenVarExpr(const VarExpr& expr);
        llvm::Value* codegenAssignExpr(const AssignExpr& expr);
        llvm::Value* codegenUpdateExpr(const UpdateExpr& expr);
        llvm::Value* codegenCallExpr(const CallExpr& expr);
        llvm::Value* codegenGetExpr(const GetExpr& expr);
        llvm::Value* codegenListExpr(const ListExpr& expr);
        llvm::Value* codegenLogicalExpr(const LogicalExpr& expr);
        llvm::Value* codegenSubscriptExpr(const SubscriptExpr& expr);
        llvm::Value* codegenRecordExpr(const RecordExpr& expr);
        llvm::Value* codegenTernaryExpr(const TernaryExpr& expr);
        llvm::Value* codegenThisExpr(const ThisExpr& expr);
        llvm::Value* codegenSuperExpr(const SuperExpr& expr);
        llvm::Value* codegenIsExpr(const IsExpr& expr);
        llvm::Value* codegenMatchExpr(const MatchExpr& expr);
        llvm::Value* codegenSizeofExpr(const SizeofExpr& expr);
        llvm::Value* codegenRetypeExpr(const RetypeExpr& expr);

        // --- Statement Codegen ---
        void codegenStmt(const std::shared_ptr<Stmt>& stmt);
        void codegenVarDecl(const VarDeclStmt& stmt);
        void codegenExpressionStmt(const ExpressionStmt& stmt);
        void codegenBlock(const BlockStmt& stmt);
        void codegenIfStmt(const IfStmt& stmt);
        void codegenWhileStmt(const WhileStmt& stmt);
        void codegenForStmt(const ForStmt& stmt);
        void codegenForInStmt(const ForInStmt& stmt);
        void codegenReturnStmt(const ReturnStmt& stmt);
        void codegenBreakStmt(const BreakStmt& stmt);
        void codegenThrowStmt(const ThrowStmt& stmt);
        void codegenTryStmt(const TryStmt& stmt);
        void codegenUnsafeBlockStmt(const UnsafeBlockStmt& stmt);

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

        // --- Runtime function declarations ---
        void declareRuntimeFunctions();
        llvm::Function* getOrDeclareRuntimeFunc(const std::string& name,
                                                 llvm::FunctionType* fn_type);

        // --- AngaraObject helpers ---
        llvm::Value* createAngaraNil();
        llvm::Value* createAngaraBool(bool val);
        llvm::Value* createAngaraI64(int64_t val);
        llvm::Value* createAngaraF64(double val);
        llvm::Value* createAngaraString(const std::string& str);
        llvm::Value* callRuntimeFunc(const std::string& name,
                                      const std::vector<llvm::Value*>& args);
        llvm::Value* extractI64(llvm::Value* obj);
        llvm::Value* extractF64(llvm::Value* obj);
        llvm::Value* extractBool(llvm::Value* obj);
        llvm::Value* extractObj(llvm::Value* obj);
        llvm::Value* extractTypeTag(llvm::Value* obj);
        llvm::Value* makeAngaraObj(llvm::Value* type_tag, llvm::Value* payload);
        llvm::Value* isTruthy(llvm::Value* obj);
        llvm::Value* isNil(llvm::Value* obj);

        // --- Variable management ---
        llvm::AllocaInst* createAlloca(llvm::Function* fn, const std::string& name);
        llvm::Value* loadVariable(const std::string& name);
        void storeVariable(const std::string& name, llvm::Value* val);

        // --- Type helpers ---
        llvm::FunctionType* getGenericAngaraFnType();
        std::string mangleName(const std::string& module, const std::string& name);
        std::string mangleMethod(const std::string& class_name, const std::string& method);

        // --- Reference data ---
        TypeChecker& m_type_checker;
        ErrorHandler& m_errorHandler;

        // --- State ---
        bool m_had_error = false;
        llvm::Triple m_target_triple;
    std::string m_module_name;
        std::string m_current_class_name;
        int m_indent_level = 0;
        int m_tmp_counter = 0;
        int m_loop_depth = 0;
        llvm::BasicBlock* m_loop_exit_block = nullptr;

        // Named values in current scope
        std::map<std::string, llvm::AllocaInst*> m_named_values;
        // Global variables
        std::map<std::string, llvm::GlobalVariable*> m_globals;

        // Output paths
        std::string m_object_file_path;
        std::string m_ir_file_path;

        // Cache for runtime function declarations
        std::map<std::string, llvm::Function*> m_runtime_funcs;

        // Type checker helpers
        const ClassType* findPropertyOwner(const ClassType* klass, const std::string& prop_name);
        const FuncStmt* findMethodAst(const ClassStmt& class_stmt, const std::string& name);
        std::string sanitizeName(const std::string& name);
    };

} // namespace angara