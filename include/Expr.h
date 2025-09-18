//
// Created by cv2 on 8/27/25.
//

#pragma once

#include <vector>
#include <string>
#include <memory>
#include <any>
#include "Token.h"
#include "ASTTypes.h"

namespace angara {

    struct Binary;
    struct Grouping;
    struct Literal;
    struct Unary;
    struct VarExpr;
    struct AssignExpr;
    struct UpdateExpr;
    struct CallExpr;
    struct GetExpr;
    struct ListExpr;
    struct LogicalExpr;
    struct SubscriptExpr;
    struct RecordExpr;
    struct TernaryExpr;
    struct ThisExpr;
    struct SuperExpr;
    struct IsExpr;

// The Visitor interface for expressions
    class ExprVisitor {
    public:
        virtual ~ExprVisitor() = default;

        virtual std::any visit(const Binary &expr) = 0;
        virtual std::any visit(const Grouping &expr) = 0;
        virtual std::any visit(const Literal &expr) = 0;
        virtual std::any visit(const Unary &expr) = 0;
        virtual std::any visit(const VarExpr &expr) = 0;
        virtual std::any visit(const AssignExpr &expr) = 0;
        virtual std::any visit(const UpdateExpr &expr) = 0;
        virtual std::any visit(const CallExpr &expr) = 0;
        virtual std::any visit(const GetExpr &expr) = 0;
        virtual std::any visit(const ListExpr &expr) = 0;
        virtual std::any visit(const LogicalExpr &expr) = 0;
        virtual std::any visit(const SubscriptExpr &expr) = 0;
        virtual std::any visit(const RecordExpr &expr) = 0;
        virtual std::any visit(const TernaryExpr &expr) = 0;
        virtual std::any visit(const ThisExpr &expr) = 0;
        virtual std::any visit(const SuperExpr &expr) = 0;
        virtual std::any visit(const IsExpr &expr) = 0;

    };

// Base class for all expression types
    struct Expr {
        virtual ~Expr() = default;

        // The accept method is a normal virtual function
        virtual std::any accept(ExprVisitor &visitor) const = 0;
    };

// All derived classes now implement the corrected accept method.
// (The implementations are identical, just the signature changes).

    struct Literal : Expr {
        Literal(Token token) : token(std::move(token)) {} // <-- Takes a Token
        std::any accept(ExprVisitor &visitor) const override { return visitor.visit(*this); }

        const Token token;
    };

    struct Binary : Expr {
        Binary(std::shared_ptr<Expr> left, Token op, std::shared_ptr<Expr> right)
                : left(std::move(left)), op(std::move(op)), right(std::move(right)) {}

        std::any accept(ExprVisitor &visitor) const override { return visitor.visit(*this); }

        const std::shared_ptr<Expr> left;
        const Token op;
        const std::shared_ptr<Expr> right;
    };

    struct Unary : Expr {
        Unary(Token op, std::shared_ptr<Expr> right)
                : op(std::move(op)), right(std::move(right)) {}

        std::any accept(ExprVisitor &visitor) const override { return visitor.visit(*this); }

        const Token op;
        const std::shared_ptr<Expr> right;
    };

    struct Grouping : Expr {
        Grouping(std::shared_ptr<Expr> expression)
                : expression(std::move(expression)) {}

        std::any accept(ExprVisitor &visitor) const override { return visitor.visit(*this); }

        const std::shared_ptr<Expr> expression;
    };

    struct VarExpr : Expr {
        VarExpr(Token name) : name(std::move(name)) {}

        std::any accept(ExprVisitor &visitor) const override { return visitor.visit(*this); }

        const Token name;
    };

    struct AssignExpr : Expr {
        const std::shared_ptr<Expr> target;
        const Token op; // The operator token: =, +=, -=, etc.
        const std::shared_ptr<Expr> value;

        // The constructor now takes the operator.
        AssignExpr(std::shared_ptr<Expr> target, Token op, std::shared_ptr<Expr> value)
                : target(std::move(target)), op(std::move(op)), value(std::move(value)) {}

        std::any accept(ExprVisitor &visitor) const override {
            return visitor.visit(*this);
        }
    };

// Represents pre/post increment/decrement, e.g., i++, --i
    struct UpdateExpr : Expr {
        UpdateExpr(std::shared_ptr<Expr> target, Token op, bool isPrefix)
                : target(std::move(target)), op(std::move(op)), isPrefix(isPrefix) {}

        std::any accept(ExprVisitor &visitor) const override { return visitor.visit(*this); }

        const std::shared_ptr<Expr> target; // The variable being updated (e.g., 'i')
        const Token op;                 // The operator token (PLUS_PLUS or MINUS_MINUS)
        const bool isPrefix;            // True for ++i, false for i++
    };

// Represents a function call expression: "callee(arguments)"
    struct CallExpr : Expr {
        CallExpr(std::shared_ptr<Expr> callee, Token paren, std::vector<std::shared_ptr<Expr>> arguments)
                : callee(std::move(callee)), paren(std::move(paren)), arguments(std::move(arguments)) {}

        std::any accept(ExprVisitor &visitor) const override { return visitor.visit(*this); }

        const std::shared_ptr<Expr> callee;
        const Token paren; // The '(' token, useful for error reporting
        const std::vector<std::shared_ptr<Expr>> arguments;
    };


// Represents property access, e.g., module.member
    struct GetExpr : Expr {
        const std::shared_ptr<Expr> object; // The thing on the left
        const Token op;                     // <-- NEW: The '.' or '?.' token
        const Token name;                   // The property name on the right

        GetExpr(std::shared_ptr<Expr> object, Token op, Token name)
            : object(std::move(object)),
              op(std::move(op)),
              name(std::move(name)) {}

        std::any accept(ExprVisitor &visitor) const override {
            return visitor.visit(*this);
        }
    };

// Represents a list literal expression, e.g., [1, 2, 3]
    struct ListExpr : Expr {
        const Token bracket; // The opening '[' token
        const std::vector<std::shared_ptr<Expr>> elements;

        ListExpr(Token bracket, std::vector<std::shared_ptr<Expr>> elements)
                : bracket(std::move(bracket)), elements(std::move(elements)) {}

        std::any accept(ExprVisitor& visitor) const override { return visitor.visit(*this); }
    };

// Represents a logical expression, e.g., left && right
    struct LogicalExpr : Expr {
        LogicalExpr(std::shared_ptr<Expr> left, Token op, std::shared_ptr<Expr> right)
                : left(std::move(left)), op(std::move(op)), right(std::move(right)) {}

        std::any accept(ExprVisitor &visitor) const override {
            return visitor.visit(*this);
        }

        const std::shared_ptr<Expr> left;
        const Token op;
        const std::shared_ptr<Expr> right;
    };

    struct SubscriptExpr : Expr {
        SubscriptExpr(std::shared_ptr<Expr> object, Token bracket, std::shared_ptr<Expr> index)
                : object(std::move(object)), bracket(std::move(bracket)), index(std::move(index)) {}

        std::any accept(ExprVisitor &visitor) const override { return visitor.visit(*this); }

        const std::shared_ptr<Expr> object; // The list object
        const Token bracket;              // The '[' token, for error reporting
        const std::shared_ptr<Expr> index;  // The expression inside the brackets
    };

// Represents a record literal, e.g., { "key": value }
    struct RecordExpr : Expr {
        // We store the keys and values as parallel vectors
        RecordExpr(std::vector<Token> keys, std::vector<std::shared_ptr<Expr>> values)
                : keys(std::move(keys)), values(std::move(values)) {}

        std::any accept(ExprVisitor &visitor) const override { return visitor.visit(*this); }

        const std::vector<Token> keys;
        const std::vector<std::shared_ptr<Expr>> values;
    };

    struct TernaryExpr : Expr {
        TernaryExpr(std::shared_ptr<Expr> condition, std::shared_ptr<Expr> thenBranch, std::shared_ptr<Expr> elseBranch)
                : condition(std::move(condition)), thenBranch(std::move(thenBranch)),
                  elseBranch(std::move(elseBranch)) {}

        std::any accept(ExprVisitor &visitor) const override { return visitor.visit(*this); }

        const std::shared_ptr<Expr> condition;
        const std::shared_ptr<Expr> thenBranch;
        const std::shared_ptr<Expr> elseBranch;
    };

// Represents the 'this' keyword
    struct ThisExpr : Expr {
        const Token keyword;

        ThisExpr(Token keyword) : keyword(std::move(keyword)) {}

        std::any accept(ExprVisitor &visitor) const override {
            return visitor.visit(*this);
        }
    };

    struct SuperExpr : Expr {
        const Token keyword; // The 'super' token
        // The method is now optional. If it's not present, it's a constructor call.
        const std::optional<Token> method;

        SuperExpr(Token keyword, std::optional<Token> method)
            : keyword(std::move(keyword)),
              method(std::move(method)) {}

        std::any accept(ExprVisitor &visitor) const override {
            return visitor.visit(*this);
        }
    };

    struct IsExpr : Expr {
        const std::shared_ptr<Expr> object;    // The expression on the left
        const Token keyword;                   // The 'is' token itself
        const std::shared_ptr<ASTType> type;   // The type on the right

        IsExpr(std::shared_ptr<Expr> object, Token keyword, std::shared_ptr<ASTType> type)
                : object(std::move(object)),
                  keyword(std::move(keyword)),
                  type(std::move(type)) {}

        std::any accept(ExprVisitor &visitor) const override {
            return visitor.visit(*this);
        }
    };
}