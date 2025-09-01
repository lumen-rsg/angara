//
// Created by cv2 on 9/1/25.
//

#pragma once

#include "Expr.h"
#include "Stmt.h"
#include "TypeChecker.h" // To access the results
#include <string>
#include <sstream>

namespace angara {

/**
 * @class ASTPrinter
 * @brief Walks a type-checked AST and prints a string representation
 *        annotated with the inferred types.
 */
    class ASTPrinter : public ExprVisitor, public StmtVisitor {
    public:
        explicit ASTPrinter(TypeChecker& checker);

        /**
         * @brief The main entry point to print a series of statements.
         * @param statements The AST to print.
         * @return A string representing the annotated AST.
         */
        std::string print(const std::vector<std::shared_ptr<Stmt>>& statements);

    private:
        // --- Visitor Methods ---
        void visit(std::shared_ptr<const VarDeclStmt> stmt) override;
        void visit(std::shared_ptr<const ExpressionStmt> stmt) override;
        // ... other statement visitors ...

        std::any visit(const Literal& expr) override;
        std::any visit(const Binary& expr) override;
        std::any visit(const VarExpr& expr) override;
        std::any visit(const Grouping& expr) override;
        // ... other expression visitors ...

        // --- Helper for formatting ---
        // Takes a name and a variable number of expressions to print.
        std::string parenthesize(const std::string& name, std::initializer_list<const Expr*> exprs);

    private:
        TypeChecker& m_type_checker;
        std::stringstream m_out;
        int m_indent_level = 0;

        void indent();

        std::string parenthesize(const std::string &name, const std::initializer_list<const Expr *> exprs,
                                 const Expr *node_for_type_lookup);

        void visit(std::shared_ptr<const BlockStmt> stmt);

        void visit(std::shared_ptr<const FuncStmt> stmt);

        void visit(std::shared_ptr<const IfStmt> stmt);

        void visit(std::shared_ptr<const WhileStmt> stmt);

        void visit(std::shared_ptr<const ForStmt> stmt);

        void visit(std::shared_ptr<const ForInStmt> stmt);

        void visit(std::shared_ptr<const ReturnStmt> stmt);

        void visit(std::shared_ptr<const ClassStmt> stmt);

        void visit(std::shared_ptr<const TraitStmt> stmt);

        void visit(std::shared_ptr<const AttachStmt> stmt);

        void visit(std::shared_ptr<const ThrowStmt> stmt);

        void visit(std::shared_ptr<const TryStmt> stmt);

        void visit(std::shared_ptr<const EmptyStmt> stmt);

        std::any visit(const Unary &expr);

        std::any visit(const LogicalExpr &expr);

        std::any visit(const AssignExpr &expr);

        std::any visit(const UpdateExpr &expr);

        std::any visit(const CallExpr &expr);

        std::any visit(const GetExpr &expr);

        std::any visit(const ListExpr &expr);

        std::any visit(const RecordExpr &expr);

        std::any visit(const SubscriptExpr &expr);

        std::any visit(const TernaryExpr &expr);

        std::any visit(const ThisExpr &expr);

        std::any visit(const SuperExpr &expr);

        std::string getTypeString(const void *node_ptr);
    };

} // namespace angara