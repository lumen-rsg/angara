//
// Created by cv2 on 25.11.2025.
//
#include "ASTPrinter.h"
#include "Colors.h"
#include <sstream>

namespace angara {

    // --- Tree Characters ---
    const char* const TREE_FORK = "├── ";
    const char* const TREE_END  = "└── ";
    const char* const TREE_DOWN = "│   ";
    const char* const TREE_EMPTY= "    ";

    ASTPrinter::ASTPrinter() {}

    void ASTPrinter::print(const std::vector<std::shared_ptr<Stmt>>& statements) {
        std::cout << CLR_BOLD << CLR_MAGENTA << "\n=== Abstract Syntax Tree ===\n" << CLR_RESET;
        for (size_t i = 0; i < statements.size(); ++i) {
            // Root level nodes
            bool isLast = (i == statements.size() - 1);
            printChild("", statements[i], isLast);
        }
        std::cout << CLR_BOLD << CLR_MAGENTA << "============================\n\n" << CLR_RESET;
    }

    // --- Helpers ---

    void ASTPrinter::printHeader(const std::string& label, const std::string& extra) {
        std::cout << m_prefix;
        std::cout << CLR_BOLD << CLR_CYAN << label << CLR_RESET;
        if (!extra.empty()) {
            std::cout << " " << CLR_YELLOW << extra << CLR_RESET;
        }
        std::cout << "\n";
    }

    void ASTPrinter::printChild(const std::string& fieldName, const std::shared_ptr<Stmt>& stmt, bool isLast) {
        if (!stmt) return;

        std::string oldPrefix = m_prefix;
        std::string oldChildPrefix = m_childPrefix;

        m_prefix = m_childPrefix + (isLast ? TREE_END : TREE_FORK);
        m_childPrefix = m_childPrefix + (isLast ? TREE_EMPTY : TREE_DOWN);

        if (!fieldName.empty()) {
            std::cout << m_prefix << CLR_DIM << fieldName << ": " << CLR_RESET;
        }

        stmt->accept(*this, stmt);

        m_prefix = oldPrefix;
        m_childPrefix = oldChildPrefix;
    }

    void ASTPrinter::printChild(const std::string& fieldName, const std::shared_ptr<Expr>& expr, bool isLast) {
        if (!expr) return;

        std::string oldPrefix = m_prefix;
        std::string oldChildPrefix = m_childPrefix;

        m_prefix = m_childPrefix + (isLast ? TREE_END : TREE_FORK);
        m_childPrefix = m_childPrefix + (isLast ? TREE_EMPTY : TREE_DOWN);

        expr->accept(*this);

        m_prefix = oldPrefix;
        m_childPrefix = oldChildPrefix;
    }

    void ASTPrinter::printChild(const std::string& fieldName, const std::shared_ptr<ClassMember>& member, bool isLast) {
        if (!member) return;

        std::string oldPrefix = m_prefix;
        std::string oldChildPrefix = m_childPrefix;

        // Calculate prefix for this node (The ClassMember wrapper)
        m_prefix = m_childPrefix + (isLast ? TREE_END : TREE_FORK);
        m_childPrefix = m_childPrefix + (isLast ? TREE_EMPTY : TREE_DOWN);

        if (auto field = std::dynamic_pointer_cast<FieldMember>(member)) {
            std::string access = (field->access == AccessLevel::PUBLIC) ? "public" : "private";

            // Print the wrapper node header
            std::cout << m_prefix << CLR_BOLD << CLR_CYAN << "FieldMember" << CLR_RESET << " " << CLR_YELLOW << access << CLR_RESET << "\n";

            // Print the underlying VarDeclStmt as a child of this wrapper
            // 'true' because it's the only child of this wrapper
            printChild("", field->declaration, true);

        } else if (auto method = std::dynamic_pointer_cast<MethodMember>(member)) {
            std::string access = (method->access == AccessLevel::PUBLIC) ? "public" : "private";

            // Print the wrapper node header
            std::cout << m_prefix << CLR_BOLD << CLR_CYAN << "MethodMember" << CLR_RESET << " " << CLR_YELLOW << access << CLR_RESET << "\n";

            // Print the underlying FuncStmt as a child of this wrapper
            printChild("", method->declaration, true);
        }

        // Restore prefixes
        m_prefix = oldPrefix;
        m_childPrefix = oldChildPrefix;
    }

    template <typename T>
    void ASTPrinter::printChildren(const std::string& listName, const std::vector<std::shared_ptr<T>>& list, bool isLastGroup) {
        if (list.empty()) return;

        // Optional: Print a group header?
        for (size_t i = 0; i < list.size(); ++i) {
            bool isLastItem = (i == list.size() - 1) && isLastGroup;
            printChild("", list[i], isLastItem);
        }
    }

    // ==================== EXPRESSIONS ====================

    std::any ASTPrinter::visit(const Binary& expr) {
        printHeader("BinaryExpr", expr.op.lexeme);
        printChild("left", expr.left, false);
        printChild("right", expr.right, true);
        return {};
    }

    std::any ASTPrinter::visit(const Grouping& expr) {
        printHeader("Grouping");
        printChild("", expr.expression, true);
        return {};
    }

    std::any ASTPrinter::visit(const Literal& expr) {
        std::string val = expr.token.lexeme;
        if (expr.token.type == TokenType::STRING) val = "\"" + val + "\"";
        printHeader("Literal", val);
        return {};
    }

    std::any ASTPrinter::visit(const Unary& expr) {
        printHeader("UnaryExpr", expr.op.lexeme);
        printChild("", expr.right, true);
        return {};
    }

    std::any ASTPrinter::visit(const VarExpr& expr) {
        printHeader("VarExpr", expr.name.lexeme);
        return {};
    }

    std::any ASTPrinter::visit(const AssignExpr& expr) {
        printHeader("AssignExpr", expr.op.lexeme);
        printChild("target", expr.target, false);
        printChild("value", expr.value, true);
        return {};
    }

    std::any ASTPrinter::visit(const UpdateExpr& expr) {
        printHeader("UpdateExpr", expr.op.lexeme + (expr.isPrefix ? " (prefix)" : " (postfix)"));
        printChild("target", expr.target, true);
        return {};
    }

    std::any ASTPrinter::visit(const CallExpr& expr) {
        printHeader("CallExpr");
        printChild("callee", expr.callee, expr.arguments.empty());
        printChildren("args", expr.arguments, true);
        return {};
    }

    std::any ASTPrinter::visit(const GetExpr& expr) {
        printHeader("GetExpr", (expr.op.type == TokenType::QUESTION_DOT ? "?." : ".") + expr.name.lexeme);
        printChild("object", expr.object, true);
        return {};
    }

    std::any ASTPrinter::visit(const ListExpr& expr) {
        printHeader("ListLiteral", "size: " + std::to_string(expr.elements.size()));
        printChildren("elements", expr.elements, true);
        return {};
    }

    std::any ASTPrinter::visit(const RecordExpr& expr) {
        printHeader("RecordLiteral");
        for (size_t i = 0; i < expr.keys.size(); ++i) {
            bool isLast = (i == expr.keys.size() - 1);

            // Create a synthetic node presentation for Key: Value
            std::string oldPrefix = m_prefix;
            std::string oldChildPrefix = m_childPrefix;

            m_prefix = m_childPrefix + (isLast ? TREE_END : TREE_FORK);
            m_childPrefix = m_childPrefix + (isLast ? TREE_EMPTY : TREE_DOWN);

            std::cout << m_prefix << CLR_GREEN << "\"" << expr.keys[i].lexeme << "\"" << CLR_RESET << ":\n";

            // Print the value as a child of the key
            printChild("val", expr.values[i], true);

            m_prefix = oldPrefix;
            m_childPrefix = oldChildPrefix;
        }
        return {};
    }

    std::any ASTPrinter::visit(const LogicalExpr& expr) {
        printHeader("LogicalExpr", expr.op.lexeme);
        printChild("left", expr.left, false);
        printChild("right", expr.right, true);
        return {};
    }

    std::any ASTPrinter::visit(const SubscriptExpr& expr) {
        printHeader("SubscriptExpr");
        printChild("object", expr.object, false);
        printChild("index", expr.index, true);
        return {};
    }

    std::any ASTPrinter::visit(const TernaryExpr& expr) {
        printHeader("TernaryExpr");
        printChild("cond", expr.condition, false);
        printChild("then", expr.thenBranch, false);
        printChild("else", expr.elseBranch, true);
        return {};
    }

    std::any ASTPrinter::visit(const ThisExpr& expr) {
        printHeader("ThisExpr");
        return {};
    }

    std::any ASTPrinter::visit(const SuperExpr& expr) {
        std::string msg = expr.method ? ("." + expr.method->lexeme) : "()";
        printHeader("SuperExpr", msg);
        return {};
    }

    std::any ASTPrinter::visit(const IsExpr& expr) {
        printHeader("IsExpr");
        printChild("object", expr.object, true);
        // Note: ASTType printing isn't fully implemented in this visitor,
        // but we could convert the type to string here.
        return {};
    }

    std::any ASTPrinter::visit(const MatchExpr& expr) {
        printHeader("MatchExpr");
        printChild("cond", expr.condition, expr.cases.empty());

        for (size_t i = 0; i < expr.cases.size(); ++i) {
            bool isLast = (i == expr.cases.size() - 1);
            const auto& c = expr.cases[i];

            // Manually handle the 'Case' structure since it's not a standard Expr
            std::string oldPrefix = m_prefix;
            std::string oldChildPrefix = m_childPrefix;

            m_prefix = m_childPrefix + (isLast ? TREE_END : TREE_FORK);
            m_childPrefix = m_childPrefix + (isLast ? TREE_EMPTY : TREE_DOWN);

            std::cout << m_prefix << CLR_BOLD << CLR_BLUE << "Case" << CLR_RESET;
            if (c.variable) std::cout << " (bind: " << c.variable->lexeme << ")";
            std::cout << "\n";

            printChild("pattern", c.pattern, false);
            printChild("body", c.body, true);

            m_prefix = oldPrefix;
            m_childPrefix = oldChildPrefix;
        }
        return {};
    }

    std::any ASTPrinter::visit(const SizeofExpr& expr) {
        printHeader("SizeofExpr");
        return {};
    }

    std::any ASTPrinter::visit(const RetypeExpr& expr) {
        printHeader("RetypeExpr");
        printChild("expr", expr.expression, true);
        return {};
    }

    // ==================== STATEMENTS ====================

    void ASTPrinter::visit(std::shared_ptr<const ExpressionStmt> stmt) {
        printHeader("ExpressionStmt");
        printChild("", stmt->expression, true);
    }

    void ASTPrinter::visit(std::shared_ptr<const VarDeclStmt> stmt) {
        std::string kind = stmt->is_const ? "ConstDecl" : "VarDecl";
        std::string info = stmt->name.lexeme;
        if (stmt->is_exported) info += " [export]";
        if (stmt->is_static) info += " [static]";

        printHeader(kind, info);
        // If we had a stringifier for ASTType, we'd print it here.
        if (stmt->initializer) {
            printChild("init", stmt->initializer, true);
        }
    }

    void ASTPrinter::visit(std::shared_ptr<const BlockStmt> stmt) {
        printHeader("BlockStmt");
        printChildren("stmts", stmt->statements, true);
    }

    void ASTPrinter::visit(std::shared_ptr<const IfStmt> stmt) {
        printHeader("IfStmt");
        if (stmt->declaration) {
            // if let
            printHeader("IfLetDecl", stmt->declaration->name.lexeme);
            printChild("init", stmt->declaration->initializer, false);
        } else {
            printChild("cond", stmt->condition, false);
        }

        printChild("then", stmt->thenBranch, stmt->elseBranch == nullptr);
        if (stmt->elseBranch) {
            printChild("else", stmt->elseBranch, true);
        }
    }

    void ASTPrinter::visit(std::shared_ptr<const EmptyStmt> stmt) {
        printHeader("EmptyStmt (;)");
    }

    void ASTPrinter::visit(std::shared_ptr<const WhileStmt> stmt) {
        printHeader("WhileStmt");
        printChild("cond", stmt->condition, false);
        printChild("body", stmt->body, true);
    }

    void ASTPrinter::visit(std::shared_ptr<const ForStmt> stmt) {
        printHeader("ForStmt");
        if (stmt->initializer) printChild("init", stmt->initializer, false);
        if (stmt->condition) printChild("cond", stmt->condition, false);
        if (stmt->increment) printChild("inc", stmt->increment, false);
        printChild("body", stmt->body, true);
    }

    void ASTPrinter::visit(std::shared_ptr<const ForInStmt> stmt) {
        printHeader("ForInStmt", stmt->name.lexeme);
        printChild("collection", stmt->collection, false);
        printChild("body", stmt->body, true);
    }

    void ASTPrinter::visit(std::shared_ptr<const FuncStmt> stmt) {
        std::string info = stmt->name.lexeme;
        if (stmt->is_foreign) info += " [foreign]";
        if (stmt->is_exported) info += " [export]";

        printHeader("FuncStmt", info);
        // Printing params is a bit manual without nodes, could list them in header
        if (stmt->body) {
            printChildren("body", *stmt->body, true);
        }
    }

    void ASTPrinter::visit(std::shared_ptr<const ReturnStmt> stmt) {
        printHeader("ReturnStmt");
        if (stmt->value) printChild("val", stmt->value, true);
    }

    void ASTPrinter::visit(std::shared_ptr<const AttachStmt> stmt) {
        std::string info = stmt->modulePath.lexeme;
        if (stmt->alias) info += " as " + stmt->alias->lexeme;
        if (!stmt->names.empty()) {
            info += " [selective: ";
            for(auto& t : stmt->names) info += t.lexeme + " ";
            info += "]";
        }
        printHeader("AttachStmt", info);
    }

    void ASTPrinter::visit(std::shared_ptr<const ThrowStmt> stmt) {
        printHeader("ThrowStmt");
        printChild("expr", stmt->expression, true);
    }

    void ASTPrinter::visit(std::shared_ptr<const TryStmt> stmt) {
        printHeader("TryStmt");
        printChild("try", stmt->tryBlock, false);

        // Catch block simulation
        std::string oldPrefix = m_prefix;
        std::string oldChildPrefix = m_childPrefix;

        m_prefix = m_childPrefix + TREE_END;
        m_childPrefix = m_childPrefix + TREE_EMPTY;

        std::cout << m_prefix << CLR_BOLD << CLR_BLUE << "Catch" << CLR_RESET
                  << " (" << stmt->catchName.lexeme << ")\n";

        printChild("body", stmt->catchBlock, true);

        m_prefix = oldPrefix;
        m_childPrefix = oldChildPrefix;
    }

    void ASTPrinter::visit(std::shared_ptr<const ClassStmt> stmt) {
        printHeader("ClassStmt", stmt->name.lexeme);
        if (stmt->superclass) printHeader("Inherits", stmt->superclass->name.lexeme);

        printChildren("members", stmt->members, true);
    }

    void ASTPrinter::visit(std::shared_ptr<const TraitStmt> stmt) {
        printHeader("TraitStmt", stmt->name.lexeme);
        printChildren("methods", stmt->methods, true);
    }

    void ASTPrinter::visit(std::shared_ptr<const ContractStmt> stmt) {
        printHeader("ContractStmt", stmt->name.lexeme);
        // Members...
    }

    void ASTPrinter::visit(std::shared_ptr<const BreakStmt> stmt) {
        printHeader("BreakStmt");
    }

    void ASTPrinter::visit(std::shared_ptr<const DataStmt> stmt) {
        printHeader("DataStmt", stmt->name.lexeme);
        printChildren("fields", stmt->fields, true);
    }

    void ASTPrinter::visit(std::shared_ptr<const EnumStmt> stmt) {
        printHeader("EnumStmt", stmt->name.lexeme);
        // Variants...
    }

    void ASTPrinter::visit(std::shared_ptr<const ForeignHeaderStmt> stmt) {
        printHeader("ForeignHeader", stmt->header.lexeme);
    }

    void ASTPrinter::visit(std::shared_ptr<const UnsafeBlockStmt> stmt) {
        printHeader("UnsafeBlock");
        printChild("block", stmt->block, true);
    }

}