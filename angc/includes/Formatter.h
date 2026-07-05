#pragma once

#include <string>
#include <sstream>
#include <vector>
#include <any>
#include "Stmt.h"
#include "Expr.h"

namespace angara {

    /// AST-based source code formatter. Traverses the AST and emits
    /// consistently formatted source code with standard indentation.
    class Formatter : public ExprVisitor, public StmtVisitor {
    public:
        /// Formats a list of top-level statements into a single source string.
        std::string format(const std::vector<std::shared_ptr<Stmt>>& statements);

        // ── Statement visitors (void, shared_ptr<const XxxStmt>)
        void visit(std::shared_ptr<const ExpressionStmt> stmt) override;
        void visit(std::shared_ptr<const VarDeclStmt> stmt) override;
        void visit(std::shared_ptr<const BlockStmt> stmt) override;
        void visit(std::shared_ptr<const IfStmt> stmt) override;
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
        void visit(std::shared_ptr<const DropStmt> stmt) override;
        void visit(std::shared_ptr<const TypeAliasStmt> stmt) override;
        void visit(std::shared_ptr<const EmptyStmt> stmt) override;

        // ── Expression visitors (std::any, const XxxExpr&)
        std::any visit(const Binary &expr) override;
        std::any visit(const Grouping &expr) override;
        std::any visit(const Literal &expr) override;
        std::any visit(const Unary &expr) override;
        std::any visit(const VarExpr &expr) override;
        std::any visit(const AssignExpr &expr) override;
        std::any visit(const UpdateExpr &expr) override;
        std::any visit(const CallExpr &expr) override;
        std::any visit(const GetExpr &expr) override;
        std::any visit(const ListExpr &expr) override;
        std::any visit(const LogicalExpr &expr) override;
        std::any visit(const SubscriptExpr &expr) override;
        std::any visit(const RecordExpr &expr) override;
        std::any visit(const TernaryExpr &expr) override;
        std::any visit(const ThisExpr &expr) override;
        std::any visit(const SuperExpr &expr) override;
        std::any visit(const IsExpr &expr) override;
        std::any visit(const CastExpr &expr) override;
        std::any visit(const DerefExpr &expr) override;
        std::any visit(const MatchExpr& expr) override;
        std::any visit(const LambdaExpr& expr) override;
        std::any visit(const RangeExpr& expr) override;
        std::any visit(const InterpStringExpr& expr) override;
        std::any visit(const TupleExpr& expr) override;  // LANG-10

    private:
        std::ostringstream m_out;
        int m_indent = 0;
        bool m_at_line_start = true;

        void writeIndent();
        void increaseIndent();
        void decreaseIndent();
        void newLine();
        void write(const std::string& s);
        void writeLine(const std::string& s);

        std::string fmtExpr(const std::shared_ptr<Expr>& expr);
        void fmtExprInto(const std::shared_ptr<Expr>& expr);
        void fmtStmt(const std::shared_ptr<Stmt>& stmt);
        void fmtBlock(const BlockStmt* block);
        void fmtMembers(const std::vector<std::shared_ptr<ClassMember>>& members);
        void fmtParams(const std::vector<Parameter>& params, bool has_this);
        void fmtType(const std::shared_ptr<ASTType>& type);
        void fmtAccess(AccessLevel access);
    };

} // namespace angara
