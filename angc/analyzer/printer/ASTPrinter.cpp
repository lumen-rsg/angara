#include "ASTPrinter.h"
#include "Colors.h"
#include <sstream>
#include <cstdio>

namespace angara {

    const char* const TREE_FORK = "├── ";
    const char* const TREE_END  = "└── ";
    const char* const TREE_DOWN = "│   ";
    const char* const TREE_EMPTY= "    ";

    // LANG-4: render a CHAR token's decimal-code-point lexeme back as a char
    // literal body, reversing the common escapes for readable output.
    // LANG-5: for code points > 0xFF, use \\u{XXXXXX} instead of truncated \\xHH.
    static std::string renderCharLexeme(const std::string& lexeme) {
        long cp = 0;
        try { cp = std::stol(lexeme); } catch (...) { return "?"; }
        switch (cp) {
            case '\n': return "\\n";
            case '\r': return "\\r";
            case '\t': return "\\t";
            case '\\': return "\\\\";
            case '\'': return "\\'";
            case '\0': return "\\0";
            default:
                if (cp >= 0x20 && cp < 0x7F) return std::string(1, static_cast<char>(cp));
                if (cp >= 0x80 && cp <= 0xFF) {
                    char buf[6];
                    std::snprintf(buf, sizeof(buf), "\\x%02lX", cp);
                    return buf;
                }
                // Code point > 0xFF: use Unicode escape.
                char buf[12];
                std::snprintf(buf, sizeof(buf), "\\u{%lX}", cp);
                return buf;
        }
    }

    ASTPrinter::ASTPrinter() {}

    void ASTPrinter::print(const std::vector<std::shared_ptr<Stmt>>& statements) {
        std::cout << CLR_BOLD << CLR_MAGENTA << "\n=== Abstract Syntax Tree ===\n" << CLR_RESET;
        for (size_t i = 0; i < statements.size(); ++i) {
            bool isLast = (i == statements.size() - 1);
            printChild("", statements[i], isLast);
        }
        std::cout << CLR_BOLD << CLR_MAGENTA << "============================\n\n" << CLR_RESET;
    }

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

        m_prefix = m_childPrefix + (isLast ? TREE_END : TREE_FORK);
        m_childPrefix = m_childPrefix + (isLast ? TREE_EMPTY : TREE_DOWN);

        if (auto field = std::dynamic_pointer_cast<FieldMember>(member)) {
            std::string access = (field->access == AccessLevel::PUBLIC) ? "public" : (field->access == AccessLevel::PROTECTED) ? "protected" : "private";

            std::cout << m_prefix << CLR_BOLD << CLR_CYAN << "FieldMember" << CLR_RESET << " " << CLR_YELLOW << access << CLR_RESET << "\n";
            printChild("", field->declaration, true);

        } else if (auto method = std::dynamic_pointer_cast<MethodMember>(member)) {
            std::string access = (method->access == AccessLevel::PUBLIC) ? "public" : (method->access == AccessLevel::PROTECTED) ? "protected" : "private";

            std::cout << m_prefix << CLR_BOLD << CLR_CYAN << "MethodMember" << CLR_RESET << " " << CLR_YELLOW << access << CLR_RESET << "\n";
            printChild("", method->declaration, true);
        }

        m_prefix = oldPrefix;
        m_childPrefix = oldChildPrefix;
    }

    template <typename T>
    void ASTPrinter::printChildren(const std::string& listName, const std::vector<std::shared_ptr<T>>& list, bool isLastGroup) {
        if (list.empty()) return;

        for (size_t i = 0; i < list.size(); ++i) {
            bool isLastItem = (i == list.size() - 1) && isLastGroup;
            printChild("", list[i], isLastItem);
        }
    }

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
        else if (expr.token.type == TokenType::CHAR) {
            // LANG-4: lexeme is the decimal code point; render as a char literal.
            val = "'" + renderCharLexeme(val) + "'";
        }
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
        // LANG-11: print named-argument labels if present.
        for (size_t i = 0; i < expr.arguments.size(); ++i) {
            bool isLast = (i == expr.arguments.size() - 1);
            std::string label = "arg";
            if (i < expr.arg_names.size() && expr.arg_names[i].has_value()) {
                label = expr.arg_names[i]->lexeme + ":";
            }
            std::string header = label + "[" + std::to_string(i) + "]";
            printChild(header, expr.arguments[i], isLast);
        }
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

            std::string oldPrefix = m_prefix;
            std::string oldChildPrefix = m_childPrefix;

            m_prefix = m_childPrefix + (isLast ? TREE_END : TREE_FORK);
            m_childPrefix = m_childPrefix + (isLast ? TREE_EMPTY : TREE_DOWN);

            std::cout << m_prefix << CLR_GREEN << "\"" << expr.keys[i].lexeme << "\"" << CLR_RESET << ":\n";
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
        return {};
    }

    std::any ASTPrinter::visit(const CastExpr& expr) {
        printHeader("CastExpr");
        printChild("object", expr.object, true);
        return {};
    }

    std::any ASTPrinter::visit(const DerefExpr& expr) {
        printHeader("DerefExpr");
        printChild("right", expr.right, true);
        return {};
    }

    std::any ASTPrinter::visit(const MatchExpr& expr) {
        printHeader("MatchExpr");
        printChild("cond", expr.condition, expr.cases.empty());

        for (size_t i = 0; i < expr.cases.size(); ++i) {
            bool isLast = (i == expr.cases.size() - 1);
            const auto& c = expr.cases[i];

            std::string oldPrefix = m_prefix;
            std::string oldChildPrefix = m_childPrefix;

            m_prefix = m_childPrefix + (isLast ? TREE_END : TREE_FORK);
            m_childPrefix = m_childPrefix + (isLast ? TREE_EMPTY : TREE_DOWN);

            std::cout << m_prefix << CLR_BOLD << CLR_BLUE << "Case" << CLR_RESET;
            if (!c.variables.empty()) {
                std::cout << " (bind: ";
                for (size_t vi = 0; vi < c.variables.size(); ++vi) {
                    if (vi > 0) std::cout << ", ";
                    std::cout << c.variables[vi].lexeme;
                }
                std::cout << ")";
            }
            if (c.guard) std::cout << " [guard]";
            std::cout << "\n";

            if (c.patterns.size() > 1) {
                // Or-pattern: print each alternative
                for (size_t pi = 0; pi < c.patterns.size(); ++pi) {
                    bool lastPat = (pi == c.patterns.size() - 1) && !c.guard;
                    printChild(pi == 0 ? "or-patterns" : "", c.patterns[pi], lastPat && !c.guard);
                }
            } else if (!c.patterns.empty()) {
                printChild("pattern", c.patterns[0], !c.guard);
            }
            if (c.guard) {
                printChild("guard", *c.guard, true);
            }
            printChild("body", c.body, true);

            m_prefix = oldPrefix;
            m_childPrefix = oldChildPrefix;
        }
        return {};
    }

    void ASTPrinter::visit(std::shared_ptr<const ExpressionStmt> stmt) {
        printHeader("ExpressionStmt");
        printChild("", stmt->expression, true);
    }

    void ASTPrinter::visit(std::shared_ptr<const VarDeclStmt> stmt) {
        std::string kind = stmt->is_const ? "ConstDecl" : "VarDecl";
        std::string info;
        // LANG-10: show destructure names if present
        if (!stmt->destructure_names.empty()) {
            info = "(";
            for (size_t i = 0; i < stmt->destructure_names.size(); ++i) {
                if (i > 0) info += ", ";
                info += stmt->destructure_names[i].lexeme;
            }
            info += ")";
        } else {
            info = stmt->name.lexeme;
        }
        if (stmt->is_exported) info += " [export]";
        if (stmt->is_static) info += " [static]";

        printHeader(kind, info);
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
        // LANG-10: show destructure names if present
        std::string info;
        if (!stmt->destructure_names.empty()) {
            info = "(";
            for (size_t i = 0; i < stmt->destructure_names.size(); ++i) {
                if (i > 0) info += ", ";
                info += stmt->destructure_names[i].lexeme;
            }
            info += ")";
        } else {
            info = stmt->name.lexeme;
        }
        printHeader("ForInStmt", info);
        printChild("collection", stmt->collection, false);
        printChild("body", stmt->body, true);
    }

    void ASTPrinter::visit(std::shared_ptr<const FuncStmt> stmt) {
        std::string info = stmt->name.lexeme;
        if (stmt->is_async) info += " [async]";
        if (stmt->is_foreign) info += " [foreign]";
        if (stmt->is_exported) info += " [export]";

        printHeader("FuncStmt", info);
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
    }

    void ASTPrinter::visit(std::shared_ptr<const BreakStmt> stmt) {
        printHeader("BreakStmt");
    }

    void ASTPrinter::visit(std::shared_ptr<const ContinueStmt> stmt) {
        printHeader("ContinueStmt");
    }

    void ASTPrinter::visit(std::shared_ptr<const DataStmt> stmt) {
        printHeader("DataStmt", stmt->name.lexeme);
        printChildren("fields", stmt->fields, true);
    }

    void ASTPrinter::visit(std::shared_ptr<const EnumStmt> stmt) {
        std::string header = stmt->name.lexeme;
        // LANG-8: include type params in debug output
        if (!stmt->type_params.empty()) {
            header += "<";
            for (size_t i = 0; i < stmt->type_params.size(); ++i) {
                if (i > 0) header += ", ";
                header += stmt->type_params[i].lexeme;
            }
            header += ">";
        }
        printHeader("EnumStmt", header);
    }

    void ASTPrinter::visit(std::shared_ptr<const UnsafeBlockStmt> stmt) {
        printHeader("UnsafeBlock");
        printChild("block", stmt->block, true);
    }

    std::any ASTPrinter::visit(const LambdaExpr& expr) {
        printHeader("LambdaExpr");
        for (size_t i = 0; i < expr.body.size(); ++i) {
            bool isLast = (i == expr.body.size() - 1);
            printChild("stmt", expr.body[i], isLast);
        }
        return {};
    }

    std::any ASTPrinter::visit(const RangeExpr& expr) {
        printHeader("RangeExpr [" + expr.op.lexeme + "]");
        printChild("left", expr.left, false);
        printChild("right", expr.right, true);
        return {};
    }

    std::any ASTPrinter::visit(const InterpStringExpr& expr) {
        printHeader("InterpStringExpr");
        const size_t n = expr.segments.size();
        for (size_t i = 0; i < n; ++i) {
            const auto& [lit, sub] = expr.segments[i];
            bool isLast = (i == n - 1);
            // Render literal segments as Literal nodes (skipping empties that
            // are adjacent to an expression hole), and recurse into each
            // expression hole so its AST is visible underneath.
            if (!sub) {
                if (!lit.empty()) {
                    printChild("lit", std::make_shared<Literal>(
                        Token(TokenType::STRING, lit, 0, 0, nullptr)), isLast);
                }
            } else {
                printChild("expr", sub, isLast);
            }
        }
        return {};
    }

    // LANG-10
    std::any ASTPrinter::visit(const TupleExpr& expr) {
        printHeader("TupleLiteral", "size: " + std::to_string(expr.elements.size()));
        printChildren("elements", expr.elements, true);
        return {};
    }

    std::any ASTPrinter::visit(const NestedPattern& expr) {
        printHeader("NestedPattern", "bindings: " + std::to_string(expr.bindings.size()));
        printChild("constructor", expr.constructor, expr.subpatterns.empty());
        printChildren("subpatterns", expr.subpatterns, true);
        return {};
    }

    std::any ASTPrinter::visit(const AwaitExpr& expr) {
        printHeader("AwaitExpr");
        printChild("future", expr.future, true);
        return {};
    }

    void ASTPrinter::visit(std::shared_ptr<const DropStmt> stmt) {
        printHeader("DropStmt: " + stmt->name.lexeme);
    }

    void ASTPrinter::visit(std::shared_ptr<const TypeAliasStmt> stmt) {
        printHeader("TypeAliasStmt", stmt->name.lexeme);
    }

}
