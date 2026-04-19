#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include "Expr.h"
#include "Stmt.h"
#include "Token.h"

namespace angara {

    class ASTPrinter : public ExprVisitor, public StmtVisitor {
    public:
        ASTPrinter();

        // Entry point to print a list of statements (e.g. a program)
        void print(const std::vector<std::shared_ptr<Stmt>>& statements);

    private:
        // --- State Management ---
        std::string m_prefix;       // The string prefix for the current line (e.g. "│   ")
        std::string m_childPrefix;  // The prefix to pass down to children

        // --- Helpers ---
        // Prints the node header with color
        void printHeader(const std::string& label, const std::string& extra = "");

        // Helper to visit a child node, handling the tree drawing logic
        void printChild(const std::string& fieldName, const std::shared_ptr<Stmt>& stmt, bool isLast);
        void printChild(const std::string& fieldName, const std::shared_ptr<Expr>& expr, bool isLast);
        void printChild(const std::string& fieldName, const std::shared_ptr<ClassMember>& member, bool isLast);

        // Helper for vectors of nodes
        template <typename T>
        void printChildren(const std::string& listName, const std::vector<std::shared_ptr<T>>& list, bool isLastGroup);

        // --- Visitor Implementations (Expressions) ---
        std::any visit(const Binary& expr) override;
        std::any visit(const Grouping& expr) override;
        std::any visit(const Literal& expr) override;
        std::any visit(const Unary& expr) override;
        std::any visit(const VarExpr& expr) override;
        std::any visit(const AssignExpr& expr) override;
        std::any visit(const UpdateExpr& expr) override;
        std::any visit(const CallExpr& expr) override;
        std::any visit(const GetExpr& expr) override;
        std::any visit(const ListExpr& expr) override;
        std::any visit(const LogicalExpr& expr) override;
        std::any visit(const SubscriptExpr& expr) override;
        std::any visit(const RecordExpr& expr) override;
        std::any visit(const TernaryExpr& expr) override;
        std::any visit(const ThisExpr& expr) override;
        std::any visit(const SuperExpr& expr) override;
        std::any visit(const IsExpr& expr) override;
        std::any visit(const MatchExpr& expr) override;
        std::any visit(const SizeofExpr& expr) override;
        std::any visit(const RetypeExpr& expr) override;
        std::any visit(const LambdaExpr& expr) override;

        // --- Visitor Implementations (Statements) ---
        void visit(std::shared_ptr<const ExpressionStmt> stmt) override;
        void visit(std::shared_ptr<const VarDeclStmt> stmt) override;
        void visit(std::shared_ptr<const BlockStmt> stmt) override;
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
        void visit(std::shared_ptr<const ContractStmt> stmt) override;
        void visit(std::shared_ptr<const BreakStmt> stmt) override;
        void visit(std::shared_ptr<const DataStmt> stmt) override;
        void visit(std::shared_ptr<const EnumStmt> stmt) override;
        void visit(std::shared_ptr<const ForeignHeaderStmt> stmt) override;
        void visit(std::shared_ptr<const UnsafeBlockStmt> stmt) override;
    };

}