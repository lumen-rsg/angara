//
// Created by cv2 on 9/1/25.
//

#pragma once

#include "Expr.h"
#include "Stmt.h"
#include "Type.h"       // For our internal Type representation
#include "TypeChecker.h"  // To access the results of the type analysis
#include "ErrorHandler.h"
#include <sstream>
#include "SymbolTable.h"

namespace angara {

/**
 * @class CTranspiler
 * @brief Walks a type-checked AST and generates equivalent C source code.
 */
    class CTranspiler : public ExprVisitor, public StmtVisitor {
    public:
        CTranspiler(TypeChecker& type_checker, ErrorHandler& errorHandler);

        /**
         * @brief The main entry point for code generation.
         * @param statements The vector of top-level statements from the parser.
         * @return A string containing the generated C source code. Returns an empty
         *         string on failure.
         */
        std::string generate(const std::vector<std::shared_ptr<Stmt>>& statements);

    private:
        // --- Visitor Methods ---
        // Statements (append to the stream)
        std::any visit(const Literal& expr) override;
        std::any visit(const Binary& expr) override;
        std::any visit(const VarExpr& expr) override;
        std::any visit(const Unary& expr) override;
        std::any visit(const Grouping& expr) override;
        std::any visit(const ListExpr& expr) override;
        std::any visit(const AssignExpr &expr) override;
        std::any visit(const UpdateExpr &expr) override;
        std::any visit(const CallExpr &expr) override;
        std::any visit(const GetExpr &expr) override;
        std::any visit(const LogicalExpr &expr) override;
        std::any visit(const SubscriptExpr &expr) override;
        std::any visit(const RecordExpr &expr) override;
        std::any visit(const TernaryExpr &expr) override;
        std::any visit(const ThisExpr &expr) override;
        std::any visit(const SuperExpr &expr) override;

        void visit(std::shared_ptr<const VarDeclStmt> stmt) override;
        void visit(std::shared_ptr<const IfStmt> stmt) override;
        void visit(std::shared_ptr<const EmptyStmt> stmt) override;
        void visit(std::shared_ptr<const WhileStmt> stmt) override;
        void visit(std::shared_ptr<const ForStmt> stmt) override;
        void visit(std::shared_ptr<const ForInStmt> stmt) override;
        void visit(std::shared_ptr<const FuncStmt> stmt) override;
        void visit(std::shared_ptr<const ReturnStmt> stmt) override;
        void visit(std::shared_ptr<const AttachStmt> stmt) override;
        void visit(std::shared_ptr<const ThrowStmt> stmt) override;
        void visit(std::shared_ptr<const TryStmt> stmt) override;
        void visit(std::shared_ptr<const ClassStmt> stmt) override;
        void visit(std::shared_ptr<const TraitStmt> stmt) override;
        void visit(std::shared_ptr<const ExpressionStmt> stmt) override;
        void visit(std::shared_ptr<const BlockStmt> stmt) override;

        // --- Helper Methods ---
        // Converts an Angara Type into a C type string (e.g., "int64_t")
        std::string getCType(const std::shared_ptr<Type>& angaraType);

    private:
        std::stringstream m_out;
        TypeChecker& m_type_checker;
        ErrorHandler& m_errorHandler;

        // For managing indentation
        int m_indent_level = 0;
        std::stringstream m_globals; // For structs, forward decls, and functions
        void indent();

        SymbolTable m_symbols; // The transpiler's own symbol table.

        bool m_hadError = false;

        void transpileListDeclaration(const VarDeclStmt &stmt, const ListExpr &list_expr);

        void transpileRecordDeclaration(const VarDeclStmt &stmt, const RecordExpr &record_expr);

        void transpileMethod(const ClassType &klass, const FuncStmt &stmt);

        std::string transpileConstructorCall(const ClassType &class_type, const vector<std::shared_ptr<Expr>> &args);
    };

} // namespace angara