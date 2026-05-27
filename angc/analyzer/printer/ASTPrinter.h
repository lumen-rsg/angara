#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include "Expr.h"
#include "Stmt.h"
#include "Token.h"

namespace angara {

    /// Pretty-prints an AST as a colored tree to stdout.
    /// Implements both ExprVisitor and StmtVisitor to traverse every node type.
    class ASTPrinter : public ExprVisitor, public StmtVisitor {
    public:
        ASTPrinter();

        /// Prints the full AST for a list of top-level statements.
        /// @param statements  The program's top-level statement list.
        void print(const std::vector<std::shared_ptr<Stmt>>& statements);

    private:
        std::string m_prefix;
        std::string m_childPrefix;

        /// Prints a colored node header line (e.g. "FuncStmt main [export]").
        /// @param label  The node type name.
        /// @param extra  Optional trailing info (name, operator, etc.).
        void printHeader(const std::string& label, const std::string& extra = "");

        /// Visits a child statement node, drawing the appropriate tree connector.
        /// @param fieldName  Optional label printed before the node (e.g. "body").
        /// @param stmt       The child statement to print.
        /// @param isLast     Whether this is the last sibling in its group.
        void printChild(const std::string& fieldName, const std::shared_ptr<Stmt>& stmt, bool isLast);

        /// Visits a child expression node, drawing the appropriate tree connector.
        /// @param fieldName  Optional label printed before the node.
        /// @param expr       The child expression to print.
        /// @param isLast     Whether this is the last sibling in its group.
        void printChild(const std::string& fieldName, const std::shared_ptr<Expr>& expr, bool isLast);

        /// Visits a class member node (field or method), unwrapping its access level.
        /// @param fieldName  Optional label printed before the node.
        /// @param member     The class member to print.
        /// @param isLast     Whether this is the last sibling in its group.
        void printChild(const std::string& fieldName, const std::shared_ptr<ClassMember>& member, bool isLast);

        /// Iterates over a vector of child nodes, printing each with tree connectors.
        /// @tparam T        Statement or expression pointer type.
        /// @param listName   Label for the group (currently unused for rendering).
        /// @param list       The vector of child nodes.
        /// @param isLastGroup  Whether this group is the last child of its parent.
        template <typename T>
        void printChildren(const std::string& listName, const std::vector<std::shared_ptr<T>>& list, bool isLastGroup);

        // --- Expression visitors ---

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
        std::any visit(const LambdaExpr& expr) override;

        // --- Statement visitors ---

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
        void visit(std::shared_ptr<const ContinueStmt> stmt) override;
        void visit(std::shared_ptr<const DataStmt> stmt) override;
        void visit(std::shared_ptr<const EnumStmt> stmt) override;
        void visit(std::shared_ptr<const UnsafeBlockStmt> stmt) override;
    };

}
