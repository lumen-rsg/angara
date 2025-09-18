//
// Created by cv2 on 9/1/25.
//

#include "ASTPrinter.h"
#include <initializer_list>

namespace angara {

    ASTPrinter::ASTPrinter(TypeChecker& checker) : m_type_checker(checker) {}

    void ASTPrinter::indent() {
        for (int i = 0; i < m_indent_level; ++i) {
            m_out << "  ";
        }
    }

    std::string ASTPrinter::print(const std::vector<std::shared_ptr<Stmt>>& statements) {
        m_out.str(""); // Clear the stream
        m_indent_level = 0;
        for (const auto& stmt : statements) {
            if (stmt) {
                stmt->accept(*this, stmt);
            }
        }
        return m_out.str();
    }

// --- Helper to format expressions like: `(op lhs rhs) : type` ---
    std::string ASTPrinter::parenthesize(const std::string& name, std::initializer_list<const Expr*> exprs, const Expr* node_for_type_lookup) {
        std::stringstream ss;
        ss << "(" << name;
        for (const auto* expr : exprs) {
            if (expr) ss << " " << std::any_cast<std::string>(const_cast<Expr*>(expr)->accept(*this));
        }
        ss << ") : " << getTypeString(node_for_type_lookup);
        return ss.str();
    }

    // Helper to safely get a type string from the checker's results.
    std::string ASTPrinter::getTypeString(const void* node_ptr) {
        if (!node_ptr) return "<null_ptr>";

        // Check expression map first
        auto expr_it = m_type_checker.m_expression_types.find(static_cast<const Expr*>(node_ptr));
        if (expr_it != m_type_checker.m_expression_types.end()) {
            return expr_it->second->toString();
        }

        // Check variable map
        auto var_it = m_type_checker.m_variable_types.find(static_cast<const VarDeclStmt*>(node_ptr));
        if (var_it != m_type_checker.m_variable_types.end()) {
            return var_it->second->toString();
        }

        return "<unresolved_type>";
    }


// --- Statement Visitors ---

    void ASTPrinter::visit(std::shared_ptr<const VarDeclStmt> stmt) {
        indent();
        m_out << "(let " << stmt->name.lexeme;

        // Get the variable's final, canonical type from the checker
        auto type_it = m_type_checker.m_variable_types.find(stmt.get());
        if (type_it != m_type_checker.m_variable_types.end()) {
            m_out << " : " << type_it->second->toString();
        }

        if (stmt->initializer) {
            m_out << " = " << std::any_cast<std::string>(stmt->initializer->accept(*this));
        }
        m_out << ")\n";
    }

    void ASTPrinter::visit(std::shared_ptr<const ExpressionStmt> stmt) {
        indent();
        m_out << "(expr_stmt " << std::any_cast<std::string>(stmt->expression->accept(*this)) << ")\n";
    }

    void ASTPrinter::visit(std::shared_ptr<const BlockStmt> stmt) {
        indent();
        m_out << "{\n";
        m_indent_level++;
        for (const auto& statement : stmt->statements) {
            statement->accept(*this, statement);
        }
        m_indent_level--;
        indent();
        m_out << "}\n";
    }

    void ASTPrinter::visit(std::shared_ptr<const FuncStmt> stmt) {
        indent();
        m_out << "(func " << stmt->name.lexeme << " ...)\n"; // Body is complex, keep it simple
    }

    void ASTPrinter::visit(std::shared_ptr<const IfStmt> stmt) {
        indent();
        m_out << "(if " << std::any_cast<std::string>(stmt->condition->accept(*this)) << "\n";
        m_indent_level++;
        stmt->thenBranch->accept(*this, stmt->thenBranch);
        if (stmt->elseBranch) {
            stmt->elseBranch->accept(*this, stmt->elseBranch);
        }
        m_indent_level--;
        indent();
        m_out << ")\n";
    }

    void ASTPrinter::visit(std::shared_ptr<const WhileStmt> stmt) {
        indent();
        m_out << "(while " << std::any_cast<std::string>(stmt->condition->accept(*this)) << "\n";
        m_indent_level++;
        stmt->body->accept(*this, stmt->body);
        m_indent_level--;
        indent();
        m_out << ")\n";
    }

    void ASTPrinter::visit(std::shared_ptr<const ForStmt> stmt) {
        indent();
        m_out << "(for\n";
        m_indent_level++;

        if (stmt->initializer) {
            indent();
            m_out << "init: ";
            // An initializer is a statement, so it doesn't return a string.
            // We call accept on it, and it will print itself.
            stmt->initializer->accept(*this, stmt->initializer);
        }

        if (stmt->condition) {
            indent();
            m_out << "cond: " << std::any_cast<std::string>(stmt->condition->accept(*this)) << "\n";
        }

        if (stmt->increment) {
            indent();
            m_out << "inc: " << std::any_cast<std::string>(stmt->increment->accept(*this)) << "\n";
        }

        indent();
        m_out << "body:\n";
        stmt->body->accept(*this, stmt->body);

        m_indent_level--;
        indent();
        m_out << ")\n";
    }

    void ASTPrinter::visit(std::shared_ptr<const ForInStmt> stmt) {
        indent();
        m_out << "(for " << stmt->name.lexeme << " in " << std::any_cast<std::string>(stmt->collection->accept(*this)) << "\n";
        m_indent_level++;
        stmt->body->accept(*this, stmt->body);
        m_indent_level--;
        indent();
        m_out << ")\n";
    }

    void ASTPrinter::visit(std::shared_ptr<const ReturnStmt> stmt) {
        indent();
        m_out << "(return";
        if (stmt->value) {
            m_out << " " << std::any_cast<std::string>(stmt->value->accept(*this));
        }
        m_out << ")\n";
    }

// ... (Stubs for Class, Trait, etc. can be simple like FuncStmt)
    void ASTPrinter::visit(std::shared_ptr<const ClassStmt> stmt) { indent(); m_out << "(class " << stmt->name.lexeme << " ...)\n"; }
    void ASTPrinter::visit(std::shared_ptr<const TraitStmt> stmt) { indent(); m_out << "(trait " << stmt->name.lexeme << " ...)\n"; }
    void ASTPrinter::visit(std::shared_ptr<const AttachStmt> stmt) { indent(); m_out << "(attach ...)\n"; }
    void ASTPrinter::visit(std::shared_ptr<const ThrowStmt> stmt) { indent(); m_out << "(throw ...)\n"; }
    void ASTPrinter::visit(std::shared_ptr<const TryStmt> stmt) { indent(); m_out << "(try ... catch ...)\n"; }
    void ASTPrinter::visit(std::shared_ptr<const EmptyStmt> stmt) { /* Do nothing */ }


// --- Expression Visitors ---

    std::any ASTPrinter::visit(const Literal& expr) {
        return parenthesize(expr.token.lexeme, {}, &expr);
    }

    std::any ASTPrinter::visit(const VarExpr& expr) {
        return parenthesize(expr.name.lexeme, {}, &expr);
    }
    std::any ASTPrinter::visit(const Unary& expr) {
        return parenthesize(expr.op.lexeme, {expr.right.get()}, &expr);
    }

    std::any ASTPrinter::visit(const Binary& expr) {
        return parenthesize(expr.op.lexeme, {expr.left.get(), expr.right.get()}, &expr);
    }

    std::any ASTPrinter::visit(const LogicalExpr& expr) {
        return parenthesize(expr.op.lexeme, {expr.left.get(), expr.right.get()}, &expr);
    }

    std::any ASTPrinter::visit(const Grouping& expr) {
        // Pass the original Grouping node pointer to look up its type
        return parenthesize("group", {expr.expression.get()}, &expr);
    }

    std::any ASTPrinter::visit(const AssignExpr& expr) {
        return parenthesize(expr.op.lexeme, {expr.target.get(), expr.value.get()}, &expr);
    }

    std::any ASTPrinter::visit(const UpdateExpr& expr) {
        std::string name = expr.isPrefix ? "pre" + expr.op.lexeme : "post" + expr.op.lexeme;
        return parenthesize(name, {expr.target.get()}, &expr);
    }

    std::any ASTPrinter::visit(const CallExpr& expr) {
        // This one is more complex to show all args
        std::stringstream ss;
        ss << "(call " << std::any_cast<std::string>(expr.callee->accept(*this));
        for (const auto& arg : expr.arguments) {
            ss << " " << std::any_cast<std::string>(arg->accept(*this));
        }
        ss << ")";

        auto type_it = m_type_checker.m_expression_types.find(&expr);
        if (type_it != m_type_checker.m_expression_types.end()) {
            ss << " : " << type_it->second->toString();
        }
        return ss.str();
    }

    std::any ASTPrinter::visit(const GetExpr& expr) {
        return parenthesize("." + expr.name.lexeme, {expr.object.get()}, &expr);
    }

    std::any ASTPrinter::visit(const ListExpr& expr) {
        return parenthesize("list-literal", {}, &expr); // Keep it simple
    }

    std::any ASTPrinter::visit(const RecordExpr& expr) {
        return parenthesize("record-literal", {}, &expr); // Keep it simple
    }

    std::any ASTPrinter::visit(const SubscriptExpr& expr) {
        return parenthesize("[]", {expr.object.get(), expr.index.get()}, &expr);
    }

    std::any ASTPrinter::visit(const TernaryExpr& expr) {
        return parenthesize("?:", {expr.condition.get(), expr.thenBranch.get(), expr.elseBranch.get()}, &expr);
    }

    std::any ASTPrinter::visit(const ThisExpr& expr) {
        return parenthesize("this", {}, &expr);
    }

    std::any ASTPrinter::visit(const SuperExpr& expr) {
        return parenthesize("super." + expr.method->lexeme, {}, &expr);
    }


} // namespace angara