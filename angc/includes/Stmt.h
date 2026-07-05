//
// Created by cv2 on 8/27/25.
//

#pragma once

#include <vector>
#include <map>
#include <memory>
#include <optional>
#include <cstdint>
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
    struct ContractStmt;
    struct BreakStmt;
    struct ContinueStmt;
    struct DataStmt;
    struct EnumStmt;
    struct UnsafeBlockStmt;
    struct DropStmt;
    struct TypeAliasStmt;

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
        virtual void visit(std::shared_ptr<const ContractStmt> stmt) = 0;
        virtual void visit(std::shared_ptr<const BreakStmt> stmt) = 0;
        virtual void visit(std::shared_ptr<const ContinueStmt> stmt) = 0;
        virtual void visit(std::shared_ptr<const DataStmt> stmt) = 0;
        virtual void visit(std::shared_ptr<const EnumStmt> stmt) = 0;
        virtual void visit(std::shared_ptr<const UnsafeBlockStmt> stmt) = 0;
        virtual void visit(std::shared_ptr<const DropStmt> stmt) = 0;
        virtual void visit(std::shared_ptr<const TypeAliasStmt> stmt) = 0;
    };
    // A simple struct to pair a parameter's name with its type annotation.
    struct Parameter {
        Token name;
        std::shared_ptr<ASTType> type;
        bool is_variadic = false;
        // LANG-11: default argument value (nullptr = required, no default)
        std::shared_ptr<Expr> default_value;
    };

    struct ClassMember {
        virtual ~ClassMember() = default;
    };

    // Represents a field declaration inside a class (e.g., 'let x as i64;')
    struct FieldMember final : ClassMember {
        const std::shared_ptr<VarDeclStmt> declaration;
        const AccessLevel access;

        FieldMember(std::shared_ptr<VarDeclStmt> decl, const AccessLevel access)
                : declaration(std::move(decl)), access(access) {}
    };

    // Represents a method declaration inside a class (e.g., 'func my_method(...)')
    struct MethodMember final : ClassMember {
        const std::shared_ptr<FuncStmt> declaration;
        const AccessLevel access;

        MethodMember(std::shared_ptr<FuncStmt> decl, const AccessLevel access)
                : declaration(std::move(decl)), access(access) {}
    };

    // Base class for all statements
    struct Stmt {
        virtual ~Stmt() = default;

        virtual void accept(StmtVisitor &visitor, std::shared_ptr<const Stmt> self) = 0;
    };

    // Derived classes
    struct ExpressionStmt final : Stmt {
        const std::shared_ptr<Expr> expression;

        explicit ExpressionStmt(std::shared_ptr<Expr> expression) : expression(std::move(expression)) {}

        void accept(StmtVisitor &visitor, const std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const ExpressionStmt>(self));
        }
    };

    struct VarDeclStmt final : Stmt {
        const Token name;
        const std::shared_ptr<ASTType> typeAnnotation;
        const std::shared_ptr<Expr> initializer;

        bool is_static = false;
        const bool is_const;
        bool is_exported = false;
        bool is_unsafe = false;
        bool is_foreign = false;

        // LANG-10: when non-empty, this is a destructuring declaration
        // (e.g. `let (a, b) = expr;`). `name` is unused in this case.
        const std::vector<Token> destructure_names;

        VarDeclStmt(Token name, std::shared_ptr<ASTType> type, std::shared_ptr<Expr> initializer, const bool is_const)
                : name(std::move(name)),
                  typeAnnotation(std::move(type)),
                  initializer(std::move(initializer)),
                  is_const(is_const) {}

        // LANG-10: constructor for destructuring declarations
        VarDeclStmt(std::vector<Token> destructure_names, std::shared_ptr<ASTType> type,
                    std::shared_ptr<Expr> initializer, const bool is_const)
                : name(Token{}),  // unused
                  typeAnnotation(std::move(type)),
                  initializer(std::move(initializer)),
                  is_const(is_const),
                  destructure_names(std::move(destructure_names)) {}

        void accept(StmtVisitor& visitor, const std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const VarDeclStmt>(self));
        }
    };

    // Represents a { ... } block of statements
    struct BlockStmt final : Stmt {
        std::vector<std::shared_ptr<Stmt>> statements;

        explicit BlockStmt(std::vector<std::shared_ptr<Stmt>> statements)
                : statements(std::move(statements)) {}

        void accept(StmtVisitor &visitor, const std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const BlockStmt>(self));
        }
    };

    // Represents an if-orif-else chain
    struct IfStmt final : Stmt {
        const Token keyword;
        const std::shared_ptr<Expr> condition;
        const std::shared_ptr<Stmt> thenBranch;
        const std::shared_ptr<Stmt> elseBranch;
        const std::shared_ptr<VarDeclStmt> declaration;

        IfStmt(Token keyword, std::shared_ptr<Expr> condition, std::shared_ptr<Stmt> thenBranch,
               std::shared_ptr<Stmt> elseBranch, const std::shared_ptr<VarDeclStmt>& declaration)
                : keyword(std::move(keyword)),
                  condition(std::move(condition)),
                  thenBranch(std::move(thenBranch)),
                  elseBranch(std::move(elseBranch)), declaration(declaration) {}

        void accept(StmtVisitor& visitor, const std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const IfStmt>(self));
        }
    };

    // Represents a lone semicolon ';'
    struct EmptyStmt final : Stmt {
        EmptyStmt() = default;

        void accept(StmtVisitor &visitor, const std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const EmptyStmt>(self));
        }
    };

    // Represents a while loop
    struct WhileStmt final : Stmt {
        const Token keyword;
        const std::shared_ptr<Expr> condition;
        const std::shared_ptr<Stmt> body;

        WhileStmt(Token keyword, std::shared_ptr<Expr> condition, std::shared_ptr<Stmt> body)
                : keyword(std::move(keyword)),
                  condition(std::move(condition)),
                  body(std::move(body)) {}

        void accept(StmtVisitor& visitor, const std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const WhileStmt>(self));
        }
    };

    // Represents a C-style for loop
    struct ForStmt final : Stmt {
        const Token keyword;
        const std::shared_ptr<Stmt> initializer;
        const std::shared_ptr<Expr> condition;
        const std::shared_ptr<Expr> increment;
        const std::shared_ptr<Stmt> body;

        ForStmt(Token keyword, std::shared_ptr<Stmt> initializer, std::shared_ptr<Expr> condition,
                std::shared_ptr<Expr> increment, std::shared_ptr<Stmt> body)
                : keyword(std::move(keyword)),
                  initializer(std::move(initializer)),
                  condition(std::move(condition)),
                  increment(std::move(increment)),
                  body(std::move(body)) {}

        void accept(StmtVisitor &visitor, const std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const ForStmt>(self));
        }
    };

    // Represents 'foreach' (for ... in ...)
    struct ForInStmt final : Stmt {
        ForInStmt(Token keyword, Token name, std::shared_ptr<Expr> collection, std::shared_ptr<Stmt> body)
                : keyword(std::move(keyword)),
                name(std::move(name)),
                collection(std::move(collection)),
                body(std::move(body)) {}

        // LANG-10: constructor for destructuring for-in (e.g. for (k, v) in map)
        ForInStmt(Token keyword, std::vector<Token> destructure_names,
                  std::shared_ptr<Expr> collection, std::shared_ptr<Stmt> body)
                : keyword(std::move(keyword)),
                  name(Token{}),  // unused
                  collection(std::move(collection)),
                  body(std::move(body)),
                  destructure_names(std::move(destructure_names)) {}

        void accept(StmtVisitor &visitor, const std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const ForInStmt>(self));
        }

        const Token keyword;
        const Token name;
        const std::shared_ptr<Expr> collection;
        const std::shared_ptr<Stmt> body;
        // LANG-10: when non-empty, destructure each iteration element
        const std::vector<Token> destructure_names;

    };

    // Represents a function declaration ("func name(...) { ... }")
    struct FuncStmt final : Stmt {
        const Token name;
        const std::vector<Parameter> params;
        // An optional return type. If not present, it's a 'void' (or 'nil') function.
        const std::shared_ptr<ASTType> returnType;

        const bool has_this;

        const std::optional<std::vector<std::shared_ptr<Stmt>>> body;
        bool is_static = false;
        bool is_exported = false;

        bool is_foreign = false;
        bool is_intrinsic = false;
        // SIMD-2: @inline annotation — forces inlining of this function
        bool is_inline = false;
        // Stores the header name, e.g., "unistd.h"
        std::vector<Token> foreign_headers;
        // RT-1: @on_throw(<value>) — the C value to return if an Angara callback
        // passed to this foreign func throws. Applied to the first callback param.
        std::optional<int64_t> on_throw_value;

        // --- GENERIC SUPPORT ---
        // Type parameter names (e.g., {"T"} for `func identity<T>(x as T) -> T`)
        const std::vector<Token> type_params;
        // TS-2: type-param bounds (e.g., T -> Trait token for `<T: Hashable>`).
        const std::map<std::string, Token> type_param_bounds;

        // TODO ignore `throws` for now and add it when exceptions are fully implemented.

        FuncStmt(Token name, bool has_this, std::vector<Parameter> params,
         std::shared_ptr<ASTType> returnType, std::optional<std::vector<std::shared_ptr<Stmt>>> body,
         std::vector<Token> type_params = {},
         std::map<std::string, Token> type_param_bounds = {})
        : name(std::move(name)),
          params(std::move(params)),
          returnType(std::move(returnType)),
          has_this(has_this),
          body(std::move(body)),
          type_params(std::move(type_params)),
          type_param_bounds(std::move(type_param_bounds)) {}

        void accept(StmtVisitor& visitor, const std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const FuncStmt>(self));
        }
    };

    struct ReturnStmt final : Stmt {
        ReturnStmt(Token keyword, std::shared_ptr<Expr> value)
                : keyword(std::move(keyword)), value(std::move(value)) {}

        void accept(StmtVisitor &visitor, const std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const ReturnStmt>(self));
        }

        const Token keyword;
        const std::shared_ptr<Expr> value;
    };

    struct AttachStmt final : Stmt {
        // A list of specific names to import. If empty, it's a simple attach.
        const std::vector<Token> names;
        const Token modulePath;
        const std::optional<Token> alias;

        AttachStmt(std::vector<Token> names, Token modulePath, std::optional<Token> alias)
                : names(std::move(names)),
                  modulePath(std::move(modulePath)),
                  alias(std::move(alias)) {}

        void accept(StmtVisitor &visitor, const std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const AttachStmt>(self));
        }
    };

    struct ThrowStmt final : Stmt {
        ThrowStmt(Token keyword, std::shared_ptr<Expr> expression)
                : keyword(std::move(keyword)), expression(std::move(expression)) {}

        void accept(StmtVisitor &visitor, const std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const ThrowStmt>(self));
        }

        const Token keyword;
        const std::shared_ptr<Expr> expression;
    };

    struct TryStmt final : Stmt {
        const std::shared_ptr<Stmt> tryBlock;
        const Token catchName;
        const std::shared_ptr<ASTType> catchType;
        const std::shared_ptr<Stmt> catchBlock;
        const std::shared_ptr<Stmt> finallyBlock;  // v5: optional finally {}

        TryStmt(std::shared_ptr<Stmt> tryBlock, Token catchName, std::shared_ptr<ASTType> catchType, std::shared_ptr<Stmt> catchBlock, std::shared_ptr<Stmt> finallyBlock = nullptr)
                : tryBlock(std::move(tryBlock)),
                  catchName(std::move(catchName)),
                  catchType(std::move(catchType)),
                  catchBlock(std::move(catchBlock)),
                  finallyBlock(std::move(finallyBlock)) {}

        void accept(StmtVisitor& visitor, const std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const TryStmt>(self));
        }
    };

    // Represents a "class Name signs C1, C2 inherits S uses T1, T2 { ... }" statement
    struct ClassStmt final : Stmt {
        const Token name;
        const std::shared_ptr<VarExpr> superclass;
        const std::vector<std::shared_ptr<VarExpr>> contracts;
        const std::vector<std::shared_ptr<VarExpr>> traits;
        const std::vector<std::shared_ptr<ClassMember>> members;
        bool is_exported = false;

        // Constructor updated to accept the contracts vector
        ClassStmt(Token name, std::shared_ptr<VarExpr> superclass,
                  std::vector<std::shared_ptr<VarExpr>> contracts,
                  std::vector<std::shared_ptr<VarExpr>> traits,
                  std::vector<std::shared_ptr<ClassMember>> members)
                : name(std::move(name)),
                  superclass(std::move(superclass)),
                  contracts(std::move(contracts)),
                  traits(std::move(traits)),
                  members(std::move(members)) {}

        void accept(StmtVisitor& visitor, const std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const ClassStmt>(self));
        }
    };

    // A trait is essentially a class with no fields and no inheritance.
    struct TraitStmt final : Stmt {
        const Token name;
        const std::vector<std::shared_ptr<FuncStmt>> methods;
        bool is_exported = false;

        TraitStmt(Token name, std::vector<std::shared_ptr<FuncStmt>> methods)
                : name(std::move(name)), methods(std::move(methods)) {}

        void accept(StmtVisitor &visitor, const std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const TraitStmt>(self));
        }
    };

    struct ContractStmt final : Stmt {
        const Token name;
        const std::vector<std::shared_ptr<ClassMember>> members;
        bool is_exported = false;

        ContractStmt(Token name, std::vector<std::shared_ptr<ClassMember>> members)
                : name(std::move(name)), members(std::move(members)) {}

        void accept(StmtVisitor& visitor, const std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const ContractStmt>(self));
        }
    };

    struct BreakStmt final : Stmt {
        const Token keyword; // The 'break' token

        explicit BreakStmt(Token keyword) : keyword(std::move(keyword)) {}

        void accept(StmtVisitor& visitor, const std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const BreakStmt>(self));
        }
    };

    struct ContinueStmt final : Stmt {
        const Token keyword; // The 'continue' token

        explicit ContinueStmt(Token keyword) : keyword(std::move(keyword)) {}

        void accept(StmtVisitor& visitor, const std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const ContinueStmt>(self));
        }
    };

    struct DataStmt final : Stmt {
        const Token name;
        // A data block only contains a list of field declarations.
        const std::vector<std::shared_ptr<VarDeclStmt>> fields;
        // --- GENERIC SUPPORT ---
        // Type parameter names (e.g., {"T", "U"} for `data Pair<T, U>`)
        const std::vector<Token> type_params;
        // TS-2: type-param bounds (e.g., T -> Trait token for `<T: Hashable>`).
        const std::map<std::string, Token> type_param_bounds;
        bool is_exported = false;
        bool is_foreign = false;
        bool is_opaque = false;
        bool is_union = false;
        bool is_owned = false;  // v5: `owned` keyword — heap type, must be dropped

        DataStmt(Token name, std::vector<std::shared_ptr<VarDeclStmt>> fields,
                 std::vector<Token> type_params = {},
                 std::map<std::string, Token> type_param_bounds = {})
            : name(std::move(name)),
              fields(std::move(fields)),
              type_params(std::move(type_params)),
              type_param_bounds(std::move(type_param_bounds)) {}

        void accept(StmtVisitor& visitor, const std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const DataStmt>(self));
        }
    };

    // --- Helper Structs for EnumStmt ---

    // Represents a single, unnamed type parameter in an enum variant.
    // For example, the `string` in `KeyPress(string)`.
    struct EnumVariantParam {
        std::shared_ptr<ASTType> type;
    };

    // Represents a single variant (or case) within an enum.
    // For example, `KeyPress(string)` or just `North`.
    struct EnumVariant {
        const Token name;
        // The list of types the variant holds. Empty if it has no associated data.
        const std::vector<EnumVariantParam> params;

        EnumVariant(Token name, std::vector<EnumVariantParam> params)
            : name(std::move(name)),
              params(std::move(params)) {}
    };


    // --- The EnumStmt AST Node ---

    // Represents a complete "enum Name { Variant1, Variant2(...) }" statement.
    // LANG-8: supports generic enums via type_params (e.g., enum Result<T,E> { Ok(T), Err(E) })
    struct EnumStmt final : Stmt {
        const Token name;
        const std::vector<std::shared_ptr<EnumVariant>> variants;
        const std::vector<Token> type_params;
        const std::map<std::string, Token> type_param_bounds;
        bool is_exported = false;

        EnumStmt(Token name,
                 std::vector<std::shared_ptr<EnumVariant>> variants,
                 std::vector<Token> type_params = {},
                 std::map<std::string, Token> type_param_bounds = {})
            : name(std::move(name)),
              variants(std::move(variants)),
              type_params(std::move(type_params)),
              type_param_bounds(std::move(type_param_bounds)) {}

        void accept(StmtVisitor& visitor, const std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const EnumStmt>(self));
        }
    };

    struct UnsafeBlockStmt final : Stmt {
        const Token keyword; // The '@' token
        const std::shared_ptr<BlockStmt> block;

        UnsafeBlockStmt(Token keyword, std::shared_ptr<BlockStmt> block)
            : keyword(std::move(keyword)), block(std::move(block)) {}

        void accept(StmtVisitor& visitor, const std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const UnsafeBlockStmt>(self));
        }
    };

    // v5: `drop x;` — explicit deallocation via the Allocator.
    struct DropStmt final : Stmt {
        const Token name;

        DropStmt(Token name) : name(std::move(name)) {}

        void accept(StmtVisitor& visitor, const std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const DropStmt>(self));
        }
    };

    // LANG-12: `type Name = Type;` — compile-time type alias.
    struct TypeAliasStmt final : Stmt {
        const Token name;
        const std::shared_ptr<ASTType> aliased_type;
        bool is_exported = false;

        TypeAliasStmt(Token name, std::shared_ptr<ASTType> aliased_type)
            : name(std::move(name)), aliased_type(std::move(aliased_type)) {}

        void accept(StmtVisitor& visitor, const std::shared_ptr<const Stmt> self) override {
            visitor.visit(std::static_pointer_cast<const TypeAliasStmt>(self));
        }
    };
}