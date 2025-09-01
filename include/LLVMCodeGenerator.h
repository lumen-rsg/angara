//
// Created by cv2 on 9/1/25.
//

#pragma once

#include "Expr.h"
#include "Stmt.h"
#include "ErrorHandler.h"
#include <memory>
#include <map>
#include "Type.h"
#include "TypeChecker.h"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"

// --- LLVM FORWARD DECLARATIONS ---
namespace llvm {
    class Value;
    class Function;
    class Type;
}

namespace angara {

/**
 * @class LLVMCodeGenerator
 * @brief Walks a type-checked AST and generates LLVM Intermediate Representation (IR).
 */
    class LLVMCodeGenerator : public ExprVisitor, public StmtVisitor {
    public:
        LLVMCodeGenerator(TypeChecker& type_checker, ErrorHandler& errorHandler);
        ~LLVMCodeGenerator(); // Destructor is important for LLVM objects

        /**
         * @brief The main entry point for code generation.
         * @param statements The vector of top-level statements from the parser.
         * @return A unique_ptr to the generated LLVM Module, or nullptr on failure.
         */
        std::unique_ptr<llvm::Module> generate(const std::vector<std::shared_ptr<Stmt>>& statements);

    private:
        // --- Visitor Methods ---
        void visit(std::shared_ptr<const VarDeclStmt> stmt) override;
        // ... all other statement visitors ...

        // Expressions will return std::any, but we will wrap the llvm::Value* in it.
        std::any visit(const Literal& expr) override;
        // ... all other expression visitors ...

        void visit(std::shared_ptr<const ExpressionStmt> stmt) override;
        void visit(std::shared_ptr<const BlockStmt> stmt) override;
        void visit(std::shared_ptr<const IfStmt> stmt) override;
        void visit(std::shared_ptr<const WhileStmt> stmt) override;
        void visit(std::shared_ptr<const ForStmt> stmt) override;
        void visit(std::shared_ptr<const ForInStmt> stmt) override;
        void visit(std::shared_ptr<const FuncStmt> stmt) override;
        void visit(std::shared_ptr<const ReturnStmt> stmt) override;
        void visit(std::shared_ptr<const ClassStmt> stmt) override;
        void visit(std::shared_ptr<const TraitStmt> stmt) override;
        void visit(std::shared_ptr<const AttachStmt> stmt) override;
        void visit(std::shared_ptr<const ThrowStmt> stmt) override;
        void visit(std::shared_ptr<const TryStmt> stmt) override;
        void visit(std::shared_ptr<const EmptyStmt> stmt) override;


// --- Expressions ---
// For expressions, we must return a std::any, which we'll leave empty for the stubs.

        std::any visit(const Unary& expr) override;
        std::any visit(const Binary& expr) override;
        std::any visit(const LogicalExpr& expr) override;
        std::any visit(const Grouping& expr) override;
        std::any visit(const VarExpr& expr) override;
        std::any visit(const AssignExpr& expr) override;
        std::any visit(const UpdateExpr& expr) override;
        std::any visit(const CallExpr& expr) override;
        std::any visit(const GetExpr& expr) override;
        std::any visit(const ListExpr& expr) override;
        std::any visit(const RecordExpr& expr) override;
        std::any visit(const SubscriptExpr& expr) override;
        std::any visit(const TernaryExpr& expr) override;
        std::any visit(const ThisExpr& expr) override;
        std::any visit(const SuperExpr& expr) override;

    private:
        // --- LLVM Core Objects ---
        // The context is a core LLVM object that owns and manages core data structures.
        std::unique_ptr<llvm::LLVMContext> m_context;

        // The IRBuilder is a helper object that makes it easy to generate LLVM instructions.
        std::unique_ptr<llvm::IRBuilder<>> m_builder;

        // The Module is the top-level container for all the code we generate.
        // It's like a C++ translation unit (.cpp file).
        std::unique_ptr<llvm::Module> m_module;

        // --- State Management ---
        // A symbol table to map Angara variable names to their memory locations (llvm::Value*).
        std::map<std::string, llvm::Value*> m_named_values;

        // The current llvm::Function we are building.
        llvm::Function* m_current_function = nullptr;

        ErrorHandler& m_errorHandler;
        bool m_hadError = false;

        TypeChecker& m_type_checker;

        llvm::Type* getLLVMType(const std::shared_ptr<Type>& angaraType);
    };

} // namespace angara
