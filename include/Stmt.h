//
// Created by cv2 on 8/27/25.
//

#pragma once

#include <vector>
#include <string>
#include <memory>
#include "Token.h"
#include "Expr.h"
#include "ASTTypes.h"
#include "AccessLevel.h"

namespace angara {
    struct ExpressionStmt;
    struct VarDeclStmt;
    struct BlockStmt;
    struct IfStmt;
    struct EmptyStmt;
    struct WhileStmt;
    struct ForStmt;
    struct ForInStmt;
    struct FuncStmt;
    struct ReturnStmt;
    struct AttachStmt;
    struct ThrowStmt;
    struct TryStmt;
    struct ClassStmt;
    struct TraitStmt;

// Statement Visitor Interface (returns void)
    class StmtVisitor {
    public:
        virtual ~StmtVisitor() = default;
        virtual void visit(std::shared_ptr<const ExpressionStmt> stmt) = 0;
        virtual void visit(std::shared_ptr<const VarDeclStmt> stmt) = 0;
        virtual void visit(std::shared_ptr<const BlockStmt> stmt) = 0;
        virtual void visit(std::shared_ptr<const IfStmt> stmt) = 0;
        virtual void visit(std::shared_ptr<const EmptyStmt> stmt) = 0;
        virtual void visit(std::shared_ptr<const WhileStmt> stmt) = 0;
        virtual void visit(std::shared_ptr<const ForStmt> stmt) = 0;
        virtual void visit(std::shared_ptr<const ForInStmt> stmt) = 0;
        virtual void visit(std::shared_ptr<const FuncStmt> stmt) = 0;
        virtual void visit(std::shared_ptr<const ReturnStmt> stmt) = 0;
        virtual void visit(std::shared_ptr<const AttachStmt> stmt) = 0;
        virtual void visit(std::shared_ptr<const ThrowStmt> stmt) = 0;
        virtual void visit(std::shared_ptr<const TryStmt> stmt) = 0;
        virtual void visit(std::shared_ptr<const ClassStmt> stmt) = 0;
        virtual void visit(std::shared_ptr<const TraitStmt> stmt) = 0;
    };
    // A simple struct to pair a parameter's name with its type annotation.
    struct Parameter {
        Token name;
        std::shared_ptr<ASTType> type;
        bool is_variadic = false; // <-- NEW
    };

    struct ClassMember {
        virtual ~ClassMember() = default;
    };

    // Represents a field declaration inside a class (e.g., 'let x as i64;')
    struct FieldMember : ClassMember {
        const std::shared_ptr<VarDeclStmt> declaration;
        const AccessLevel access;

        FieldMember(std::shared_ptr<VarDeclStmt> decl, AccessLevel access)
                : declaration(std::move(decl)), access(access) {}
    };

    // Represents a method declaration inside a class (e.g., 'func my_method(...)')
    struct MethodMember : ClassMember {
        const std::shared_ptr<FuncStmt> declaration;
        const AccessLevel access;

        MethodMember(std::shared_ptr<FuncStmt> decl, AccessLevel access)
                : declaration(std::move(decl)), access(access) {}
    };

    // Base class for all statements
    struct Stmt {
        virtual ~Stmt() = default;

        virtual void accept(StmtVisitor &visitor, std::shared_ptr<const Stmt> self) = 0;
    };

    // Derived classes
    struct ExpressionStmt : Stmt {
        const std::shared_ptr<Expr> expression;

        ExpressionStmt(std::shared_ptr<Expr> expression) : expression(std::move(expression)) {}

        void accept(StmtVisitor &visitor, std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const ExpressionStmt>(self));
        }
    };

    struct VarDeclStmt : Stmt {
        const Token name;
        const std::shared_ptr<ASTType> typeAnnotation;
        const std::shared_ptr<Expr> initializer;

        bool is_static = false;
        const bool is_const;
        bool is_exported = false;

        VarDeclStmt(Token name, std::shared_ptr<ASTType> type, std::shared_ptr<Expr> initializer, bool is_const)
                : name(std::move(name)),
                  typeAnnotation(std::move(type)),
                  initializer(std::move(initializer)),
                  is_const(is_const) {}

        void accept(StmtVisitor& visitor, std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const VarDeclStmt>(self));
        }
    };

    // Represents a { ... } block of statements
    struct BlockStmt : Stmt {
        std::vector<std::shared_ptr<Stmt>> statements;

        BlockStmt(std::vector<std::shared_ptr<Stmt>> statements)
                : statements(std::move(statements)) {}

        void accept(StmtVisitor &visitor, std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const BlockStmt>(self));
        }
    };

    // Represents an if-orif-else chain
    struct IfStmt : Stmt {
        const Token keyword;
        const std::shared_ptr<Expr> condition;
        const std::shared_ptr<Stmt> thenBranch;
        const std::shared_ptr<Stmt> elseBranch;

        IfStmt(Token keyword, std::shared_ptr<Expr> condition, std::shared_ptr<Stmt> thenBranch, std::shared_ptr<Stmt> elseBranch)
                : keyword(std::move(keyword)),
                  condition(std::move(condition)),
                  thenBranch(std::move(thenBranch)),
                  elseBranch(std::move(elseBranch)) {}

        void accept(StmtVisitor& visitor, std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const IfStmt>(self));
        }
    };

    // Represents a lone semicolon ';'
    struct EmptyStmt : Stmt {
        EmptyStmt() = default;

        void accept(StmtVisitor &visitor, std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const EmptyStmt>(self));
        }
    };

    // Represents a while loop
    struct WhileStmt : Stmt {
        const Token keyword;
        const std::shared_ptr<Expr> condition;
        const std::shared_ptr<Stmt> body;

        WhileStmt(Token keyword, std::shared_ptr<Expr> condition, std::shared_ptr<Stmt> body)
                : keyword(std::move(keyword)),
                  condition(std::move(condition)),
                  body(std::move(body)) {}

        void accept(StmtVisitor& visitor, std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const WhileStmt>(self));
        }
    };

    // Represents a C-style for loop
    struct ForStmt : Stmt {
        ForStmt(Token keyword, std::shared_ptr<Stmt> initializer, std::shared_ptr<Expr> condition,
                std::shared_ptr<Expr> increment, std::shared_ptr<Stmt> body)
                : keyword(std::move(keyword)), initializer(std::move(initializer)), condition(std::move(condition)),
                  increment(std::move(increment)), body(std::move(body)) {}

        void accept(StmtVisitor &visitor, std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const ForStmt>(self));
        }

        const std::shared_ptr<Stmt> initializer;
        const std::shared_ptr<Expr> condition;
        const std::shared_ptr<Expr> increment;
        const std::shared_ptr<Stmt> body;
        const Token keyword;
    };

    // Represents 'foreach' (for ... in ...)
    struct ForInStmt : Stmt {
        ForInStmt(Token keyword, Token name, std::shared_ptr<Expr> collection, std::shared_ptr<Stmt> body)
                : keyword(std::move(keyword)), name(std::move(name)), collection(std::move(collection)), body(std::move(body)) {}

        void accept(StmtVisitor &visitor, std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const ForInStmt>(self));
        }

        const Token name;
        const std::shared_ptr<Expr> collection;
        const std::shared_ptr<Stmt> body;
        const Token keyword;
    };

    // Represents a function declaration ("func name(...) { ... }")
    struct FuncStmt : Stmt {
        const Token name;
        const std::vector<Parameter> params;
        // An optional return type. If not present, it's a 'void' (or 'nil') function.
        const std::shared_ptr<ASTType> returnType;

        const bool has_this;

        const std::optional<std::vector<std::shared_ptr<Stmt>>> body;
        bool is_static = false;
        bool is_exported = false;

        // TODO ignore `throws` for now and add it when exceptions are fully implemented.

        FuncStmt(Token name, bool has_this, std::vector<Parameter> params,
                 std::shared_ptr<ASTType> returnType, std::optional<std::vector<std::shared_ptr<Stmt>>> body)
                : name(std::move(name)),
                  params(std::move(params)),
                  returnType(std::move(returnType)),
                  has_this(has_this),
                  body(std::move(body)) {}

        void accept(StmtVisitor& visitor, std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const FuncStmt>(self));
        }
    };

    struct ReturnStmt : Stmt {
        ReturnStmt(Token keyword, std::shared_ptr<Expr> value)
                : keyword(std::move(keyword)), value(std::move(value)) {}

        void accept(StmtVisitor &visitor, std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const ReturnStmt>(self));
        }

        const Token keyword;
        const std::shared_ptr<Expr> value;
    };

    struct AttachStmt : Stmt {
        // A list of specific names to import. If empty, it's a simple attach.
        const std::vector<Token> names;
        const Token modulePath;
        const std::optional<Token> alias;

        AttachStmt(std::vector<Token> names, Token modulePath, std::optional<Token> alias)
                : names(std::move(names)),
                  modulePath(std::move(modulePath)),
                  alias(std::move(alias)) {}

        void accept(StmtVisitor &visitor, std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const AttachStmt>(self));
        }
    };

    struct ThrowStmt : Stmt {
        ThrowStmt(Token keyword, std::shared_ptr<Expr> expression)
                : keyword(std::move(keyword)), expression(std::move(expression)) {}

        void accept(StmtVisitor &visitor, std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const ThrowStmt>(self));
        }

        const Token keyword;
        const std::shared_ptr<Expr> expression;
    };

    struct TryStmt : Stmt {
        TryStmt(std::shared_ptr<Stmt> tryBlock, Token catchName, std::shared_ptr<Stmt> catchBlock)
                : tryBlock(std::move(tryBlock)), catchName(std::move(catchName)), catchBlock(std::move(catchBlock)) {}

        void accept(StmtVisitor &visitor, std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const TryStmt>(self));
        }

        const std::shared_ptr<Stmt> tryBlock;
        const Token catchName; // The 'e' in catch(e)
        const std::shared_ptr<Stmt> catchBlock;
    };

    // Represents a "class Name { [let field...;] [func method...;] }" statement
    struct ClassStmt : Stmt {
        const Token name;
        const std::shared_ptr<VarExpr> superclass;
        const std::vector<std::shared_ptr<VarExpr>> traits;
        const std::vector<std::shared_ptr<ClassMember>> members;
        bool is_exported = false;

        ClassStmt(Token name, std::shared_ptr<VarExpr> superclass,
                  std::vector<std::shared_ptr<VarExpr>> traits,
                  std::vector<std::shared_ptr<ClassMember>> members)
                : name(std::move(name)),
                  superclass(std::move(superclass)),
                  traits(std::move(traits)),
                  members(std::move(members)) {}

        void accept(StmtVisitor& visitor, std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const ClassStmt>(self));
        }
    };

    // A trait is essentially a class with no fields and no inheritance.
    struct TraitStmt : Stmt {
        const Token name;
        const std::vector<std::shared_ptr<FuncStmt>> methods;
        bool is_exported = false;

        TraitStmt(Token name, std::vector<std::shared_ptr<FuncStmt>> methods)
                : name(std::move(name)), methods(std::move(methods)) {}

        void accept(StmtVisitor &visitor, std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const TraitStmt>(self));
        }
    };
}