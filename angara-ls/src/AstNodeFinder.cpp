//
// Created by cv2 on 25.09.2025.
//

#include "AstNodeFinder.h"

namespace angara {

// --- Public Entry Point ---

std::shared_ptr<const Expr> AstNodeFinder::find(const std::vector<std::shared_ptr<Stmt>>& statements, const Position& target) {
    m_target_pos = target;
    m_best_match = nullptr;
    m_best_match_range = std::nullopt;

    for (const auto& stmt : statements) {
        if (stmt) {
            stmt->accept(*this, stmt);
        }
    }
    return m_best_match;
}

// --- Helper Implementations ---


    bool AstNodeFinder::position_in_range(const Range& range) const {
        if (m_target_pos.line < range.start.line || m_target_pos.line > range.end.line) {
            return false;
        }
        if (m_target_pos.line == range.start.line && m_target_pos.character < range.start.character) {
            return false;
        }
        if (m_target_pos.line == range.end.line && m_target_pos.character > range.end.character) {
            return false;
        }
        return true;
    }

Range AstNodeFinder::range_from_token(const Token& token) const {
    // LSP positions are 0-indexed, Angara tokens are 1-indexed.
    int line = token.line - 1;
    int start_char = token.column - 1;
    int end_char = start_char + token.lexeme.length();
    return {{line, start_char}, {line, end_char}};
}

    void AstNodeFinder::update_best_match(std::shared_ptr<const Expr> candidate, const Range& candidate_range) {
    if (!position_in_range(candidate_range)) {
        return;
    }

    if (!m_best_match_range.has_value() ||
        // A range is better if it starts later...
        (candidate_range.start.line > m_best_match_range->start.line ||
         (candidate_range.start.line == m_best_match_range->start.line &&
          candidate_range.start.character >= m_best_match_range->start.character))
        &&
        // ...and ends earlier. This finds the most tightly-nested node.
        (candidate_range.end.line < m_best_match_range->end.line ||
         (candidate_range.end.line == m_best_match_range->end.line &&
          candidate_range.end.character <= m_best_match_range->end.character))
       )
    {
        m_best_match = candidate;
        m_best_match_range = candidate_range;
    }
}

void AstNodeFinder::accept_optional(const std::shared_ptr<Expr>& expr) {
    if (expr) {
        expr->accept(*this);
    }
}

// --- Visitor Implementations (The Core Logic) ---

// Statements just traverse into their expressions.
void AstNodeFinder::visit(std::shared_ptr<const ExpressionStmt> stmt) { stmt->expression->accept(*this); }
void AstNodeFinder::visit(std::shared_ptr<const VarDeclStmt> stmt) { accept_optional(stmt->initializer); }
void AstNodeFinder::visit(std::shared_ptr<const BlockStmt> stmt) { for (const auto& s : stmt->statements) { if (s) s->accept(*this, s); } }
void AstNodeFinder::visit(std::shared_ptr<const IfStmt> stmt) { accept_optional(stmt->condition); if(stmt->thenBranch) stmt->thenBranch->accept(*this, stmt->thenBranch); if(stmt->elseBranch) stmt->elseBranch->accept(*this, stmt->elseBranch); }
void AstNodeFinder::visit(std::shared_ptr<const WhileStmt> stmt) { stmt->condition->accept(*this); stmt->body->accept(*this, stmt->body); }
void AstNodeFinder::visit(std::shared_ptr<const ForStmt> stmt) { if(stmt->initializer) stmt->initializer->accept(*this, stmt->initializer); accept_optional(stmt->condition); accept_optional(stmt->increment); stmt->body->accept(*this, stmt->body); }
void AstNodeFinder::visit(std::shared_ptr<const ForInStmt> stmt) { stmt->collection->accept(*this); stmt->body->accept(*this, stmt->body); }
void AstNodeFinder::visit(std::shared_ptr<const ReturnStmt> stmt) { accept_optional(stmt->value); }
void AstNodeFinder::visit(std::shared_ptr<const ThrowStmt> stmt) { stmt->expression->accept(*this); }
void AstNodeFinder::visit(std::shared_ptr<const TryStmt> stmt) { stmt->tryBlock->accept(*this, stmt->tryBlock); stmt->catchBlock->accept(*this, stmt->catchBlock); }
// Statements without expressions do nothing.
void AstNodeFinder::visit(std::shared_ptr<const EmptyStmt> stmt) {}
void AstNodeFinder::visit(std::shared_ptr<const FuncStmt> stmt) { if (stmt->body) { for (const auto& s : *stmt->body) { if (s) s->accept(*this, s); } } }
void AstNodeFinder::visit(std::shared_ptr<const ClassStmt> stmt) { for(const auto& member : stmt->members) { if(auto method = std::dynamic_pointer_cast<const MethodMember>(member)) { if (method->declaration) method->declaration->accept(*this, method->declaration); } } }
void AstNodeFinder::visit(std::shared_ptr<const AttachStmt> stmt) {}
void AstNodeFinder::visit(std::shared_ptr<const TraitStmt> stmt) {}
void AstNodeFinder::visit(std::shared_ptr<const ContractStmt> stmt) {}
void AstNodeFinder::visit(std::shared_ptr<const BreakStmt> stmt) {}
void AstNodeFinder::visit(std::shared_ptr<const DataStmt> stmt) {}
void AstNodeFinder::visit(std::shared_ptr<const EnumStmt> stmt) {}

// Expressions update the best match and traverse deeper.
std::any AstNodeFinder::visit(const Literal& expr) { update_best_match(std::make_shared<Literal>(expr), range_from_token(expr.token)); return {}; }
std::any AstNodeFinder::visit(const VarExpr& expr) { update_best_match(std::make_shared<VarExpr>(expr), range_from_token(expr.name)); return {}; }
std::any AstNodeFinder::visit(const ThisExpr& expr) { update_best_match(std::make_shared<ThisExpr>(expr), range_from_token(expr.keyword)); return {}; }
std::any AstNodeFinder::visit(const Grouping& expr) { expr.expression->accept(*this); return {}; }

std::any AstNodeFinder::visit(const Unary& expr) {
    update_best_match(std::make_shared<Unary>(expr), range_from_token(expr.op));
    expr.right->accept(*this);
    return {};
}

std::any AstNodeFinder::visit(const Binary& expr) {
    // A binary expression's range is from its left to its right. We approximate with the operator.
    update_best_match(std::make_shared<Binary>(expr), range_from_token(expr.op));
    expr.left->accept(*this);
    expr.right->accept(*this);
    return {};
}

std::any AstNodeFinder::visit(const AssignExpr& expr) {
    update_best_match(std::make_shared<AssignExpr>(expr), range_from_token(expr.op));
    expr.target->accept(*this);
    expr.value->accept(*this);
    return {};
}

std::any AstNodeFinder::visit(const UpdateExpr& expr) {
    update_best_match(std::make_shared<UpdateExpr>(expr), range_from_token(expr.op));
    expr.target->accept(*this);
    return {};
}

std::any AstNodeFinder::visit(const CallExpr& expr) {
    update_best_match(std::make_shared<CallExpr>(expr), range_from_token(expr.paren));
    expr.callee->accept(*this);
    for(const auto& arg : expr.arguments) { arg->accept(*this); }
    return {};
}

std::any AstNodeFinder::visit(const GetExpr& expr) {
    update_best_match(std::make_shared<GetExpr>(expr), range_from_token(expr.name));
    expr.object->accept(*this);
    return {};
}

std::any AstNodeFinder::visit(const ListExpr& expr) {
    update_best_match(std::make_shared<ListExpr>(expr), range_from_token(expr.bracket));
    for(const auto& elem : expr.elements) { elem->accept(*this); }
    return {};
}

std::any AstNodeFinder::visit(const LogicalExpr& expr) {
    update_best_match(std::make_shared<LogicalExpr>(expr), range_from_token(expr.op));
    expr.left->accept(*this);
    expr.right->accept(*this);
    return {};
}

std::any AstNodeFinder::visit(const SubscriptExpr& expr) {
    update_best_match(std::make_shared<SubscriptExpr>(expr), range_from_token(expr.bracket));
    expr.object->accept(*this);
    expr.index->accept(*this);
    return {};
}

std::any AstNodeFinder::visit(const RecordExpr& expr) {
    for(const auto& val : expr.values) { val->accept(*this); }
    return {};
}

std::any AstNodeFinder::visit(const TernaryExpr& expr) {
    expr.condition->accept(*this);
    expr.thenBranch->accept(*this);
    expr.elseBranch->accept(*this);
    return {};
}

std::any AstNodeFinder::visit(const SuperExpr& expr) {
    update_best_match(std::make_shared<SuperExpr>(expr), range_from_token(expr.keyword));
    return {};
}

std::any AstNodeFinder::visit(const IsExpr& expr) {
    update_best_match(std::make_shared<IsExpr>(expr), range_from_token(expr.keyword));
    expr.object->accept(*this);
    return {};
}

std::any AstNodeFinder::visit(const MatchExpr& expr) {
    update_best_match(std::make_shared<MatchExpr>(expr), range_from_token(expr.keyword));
    expr.condition->accept(*this);
    for(const auto& c : expr.cases) {
        c.pattern->accept(*this);
        c.body->accept(*this);
    }
    return {};
}

} // namespace angara