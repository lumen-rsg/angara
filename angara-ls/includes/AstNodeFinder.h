#ifndef ANGARA_LS_ASTNODEFINDER_H
#define ANGARA_LS_ASTNODEFINDER_H

#include <LspDiagnostic.h>

#include "Expr.h"
#include "Stmt.h"
#include <memory>
#include <optional>

namespace angara {

class AstNodeFinder : public StmtVisitor, public ExprVisitor {
public:
    // Finds the most specific expression node at a given position.
    std::shared_ptr<const Expr> find(const std::vector<std::shared_ptr<Stmt>>& statements, const Position& target);

private:
    // --- Visitor Overrides ---
    // Statements
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


    // Expressions
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

    // --- Helper Methods ---
    // Checks if the current target position is within a given range.
    bool position_in_range(const Range& range) const;

    // Creates a Range object from a Token.
    Range range_from_token(const Token& token) const;

    // Updates the best match if the new candidate is more specific.
    void update_best_match(std::shared_ptr<const Expr> candidate, const Range& candidate_range);

    // Helper to recursively visit an optional expression.
    void accept_optional(const std::shared_ptr<Expr>& expr);

    // --- State ---
    Position m_target_pos;
    std::shared_ptr<const Expr> m_best_match = nullptr;
    std::optional<Range> m_best_match_range = std::nullopt;
};

} // namespace angara

#endif // ANGARA_LS_ASTNODEFINDER_H