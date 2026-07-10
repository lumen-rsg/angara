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
#include <optional>

namespace angara {

    // Forward declaration — Stmt is defined in Stmt.h but LambdaExpr needs it
    struct Stmt;

    struct Binary;
    struct Grouping;
    struct Literal;
    struct Unary;
    struct NestedPattern;  // nested constructor pattern in match expressions
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
    struct CastExpr;
    struct DerefExpr;
    struct MatchExpr;
    struct LambdaExpr;
    struct RangeExpr;
    struct InterpStringExpr;
    struct TupleExpr;  // LANG-10
    struct AwaitExpr;  // LIB-4: await expression
    struct AsmExpr;    // inline assembly expression

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
        virtual std::any visit(const CastExpr &expr) = 0;
        virtual std::any visit(const DerefExpr &expr) = 0;
        virtual std::any visit(const MatchExpr& expr) = 0;
        virtual std::any visit(const LambdaExpr& expr) = 0;
        virtual std::any visit(const RangeExpr& expr) = 0;
        virtual std::any visit(const InterpStringExpr& expr) = 0;
        virtual std::any visit(const TupleExpr& expr) = 0;  // LANG-10
        virtual std::any visit(const AwaitExpr& expr) = 0;  // LIB-4
        virtual std::any visit(const AsmExpr& expr) = 0;    // inline assembly
        virtual std::any visit(const NestedPattern& expr) = 0;  // nested constructor pattern in match

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
        CallExpr(std::shared_ptr<Expr> callee, Token paren, std::vector<std::shared_ptr<Expr>> arguments,
                 std::vector<std::optional<Token>> arg_names = {})
                : callee(std::move(callee)), paren(std::move(paren)),
                  arguments(std::move(arguments)), arg_names(std::move(arg_names)) {}

        std::any accept(ExprVisitor &visitor) const override { return visitor.visit(*this); }

        const std::shared_ptr<Expr> callee;
        const Token paren; // The '(' token, useful for error reporting
        const std::vector<std::shared_ptr<Expr>> arguments;
        // LANG-11: per-argument name (nullopt = positional, Token = named arg)
        const std::vector<std::optional<Token>> arg_names;
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
        TernaryExpr(std::shared_ptr<Expr> condition, std::shared_ptr<Expr> thenBranch, std::shared_ptr<Expr> elseBranch,
                    Token op)
                : condition(std::move(condition)), thenBranch(std::move(thenBranch)),
                  elseBranch(std::move(elseBranch)), op(std::move(op)) {}

        std::any accept(ExprVisitor &visitor) const override { return visitor.visit(*this); }

        const std::shared_ptr<Expr> condition;
        const std::shared_ptr<Expr> thenBranch;
        const std::shared_ptr<Expr> elseBranch;
        const Token op;
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

    // Represents a type cast expression: expr as Type
    // Used for pointer type conversions in FFI contexts (e.g., ptr as *i64)
    struct CastExpr : Expr {
        const std::shared_ptr<Expr> object;
        const Token keyword;                   // The 'as' token
        const std::shared_ptr<ASTType> target; // The target type

        CastExpr(std::shared_ptr<Expr> object, Token keyword, std::shared_ptr<ASTType> target)
                : object(std::move(object)),
                  keyword(std::move(keyword)),
                  target(std::move(target)) {}

        std::any accept(ExprVisitor &visitor) const override {
            return visitor.visit(*this);
        }
    };

    // Represents a pointer dereference: *expr (FFI only)
    // Only valid when expr has a PointerType — enforced by the type checker.
    struct DerefExpr : Expr {
        const Token op;                    // The '*' token
        const std::shared_ptr<Expr> right; // The pointer expression

        DerefExpr(Token op, std::shared_ptr<Expr> right)
                : op(std::move(op)), right(std::move(right)) {}

        std::any accept(ExprVisitor &visitor) const override {
            return visitor.visit(*this);
        }
    };

    // A nested constructor pattern: Variant(subpattern1, subpattern2, ...)
    // Used in match expressions for deep destructuring, e.g.:
    //   case Ok(Some(v)): ...
    // The `constructor` is the outer variant (e.g. GetExpr for Result.Ok).
    // `subpatterns` are the nested patterns (may themselves be NestedPattern,
    // VarExpr, Literal, or GetExpr for leaf constructor patterns).
    // `bindings` are the variable names bound at this level (for leaf
    // constructor patterns that also name bindings, like Circle(r)).
    struct NestedPattern : Expr {
        const std::shared_ptr<Expr> constructor;
        const std::vector<std::shared_ptr<Expr>> subpatterns;
        const std::vector<Token> bindings;  // variable names bound at this level

        NestedPattern(std::shared_ptr<Expr> constructor,
                      std::vector<std::shared_ptr<Expr>> subpatterns,
                      std::vector<Token> bindings)
            : constructor(std::move(constructor)),
              subpatterns(std::move(subpatterns)),
              bindings(std::move(bindings)) {}

        std::any accept(ExprVisitor& visitor) const override {
            return visitor.visit(*this);
        }
    };

    // A single case within a match expression, e.g., `case Pattern: body`
    struct MatchCase {
        const std::vector<std::shared_ptr<Expr>> patterns; // or-patterns (at least 1), each is a Literal/VarExpr/GetExpr chain
        const std::vector<Token> variables;                  // bound payload names (merged from all alternatives)
        const std::vector<std::vector<Token>> alt_variables; // per-alternative variable lists (one per pattern in the or-group)
        const std::optional<std::shared_ptr<Expr>> guard;   // optional `if guard_expr`
        const std::shared_ptr<Expr> body;
    };

// The entire match expression, e.g., `match (x) { ... }`
    struct MatchExpr : Expr {
        const Token keyword;
        const std::shared_ptr<Expr> condition;
        const std::vector<MatchCase> cases;

        MatchExpr(Token keyword, std::shared_ptr<Expr> condition, std::vector<MatchCase> cases)
                : keyword(std::move(keyword)),
                  condition(std::move(condition)),
                  cases(std::move(cases)) {}

        std::any accept(ExprVisitor& visitor) const override {
            return visitor.visit(*this);
        }
    };

    // Represents a lambda (anonymous function) expression:
    //   func(x as i64) -> i64 { return x * 2; }
    struct LambdaExpr : Expr {
        const Token keyword;                           // The 'func' token
        const std::vector<std::shared_ptr<ASTType>> param_types;  // Parameter type annotations
        const std::vector<Token> param_names;          // Parameter name tokens
        // LANG-11: default values for lambda parameters (nullptr = required)
        const std::vector<std::shared_ptr<Expr>> param_defaults;
        const std::shared_ptr<ASTType> returnType;     // Optional return type annotation
        const std::vector<std::shared_ptr<Stmt>> body; // Lambda body statements

        LambdaExpr(Token keyword,
                   std::vector<Token> param_names,
                   std::vector<std::shared_ptr<ASTType>> param_types,
                   std::vector<std::shared_ptr<Expr>> param_defaults,
                   std::shared_ptr<ASTType> returnType,
                   std::vector<std::shared_ptr<Stmt>> body)
            : keyword(std::move(keyword)),
              param_types(std::move(param_types)),
              param_names(std::move(param_names)),
              param_defaults(std::move(param_defaults)),
              returnType(std::move(returnType)),
              body(std::move(body)) {}

        std::any accept(ExprVisitor& visitor) const override {
            return visitor.visit(*this);
        }
    };

    // LANG-1: a range expression `start..end` (exclusive) or `start...end` (inclusive).
    // The op token distinguishes DOT_DOT (exclusive) from DOT_DOT_DOT (inclusive).
    struct RangeExpr : Expr {
        const std::shared_ptr<Expr> left;
        const Token op;
        const std::shared_ptr<Expr> right;

        RangeExpr(std::shared_ptr<Expr> left, Token op, std::shared_ptr<Expr> right)
                : left(std::move(left)), op(std::move(op)), right(std::move(right)) {}

        std::any accept(ExprVisitor& visitor) const override {
            return visitor.visit(*this);
        }
    };

    // LANG-3: an interpolated string $"...{expr}...". Segments alternate between
    // literal text and parsed expressions: [lit0, expr0, lit1, expr1, ..., litN].
    // The first and last segments are always literals (possibly empty).
    struct InterpStringExpr : Expr {
        // Pairs of (literal_text, optional_expr). A trailing literal has a null expr.
        const std::vector<std::pair<std::string, std::shared_ptr<Expr>>> segments;

        explicit InterpStringExpr(std::vector<std::pair<std::string, std::shared_ptr<Expr>>> segs)
                : segments(std::move(segs)) {}

        std::any accept(ExprVisitor& visitor) const override {
            return visitor.visit(*this);
        }
    };

    // LANG-10: tuple literal expression, e.g. (1, "hello", true).
    // Distinguished from Grouping by the presence of commas (2+ elements,
    // or a trailing comma for 1 element).
    struct TupleExpr : Expr {
        const Token paren;  // the opening '(' token
        const std::vector<std::shared_ptr<Expr>> elements;

        TupleExpr(Token paren, std::vector<std::shared_ptr<Expr>> elements)
                : paren(std::move(paren)), elements(std::move(elements)) {}

        std::any accept(ExprVisitor& visitor) const override {
            return visitor.visit(*this);
        }
    };

    // LIB-4: await expression — suspends the current async function until
    // the awaited future resolves. Consumes the future (move semantics).
    // Only valid inside `async func` bodies.
    struct AwaitExpr : Expr {
        const Token keyword;                    // the 'await' token
        const std::shared_ptr<Expr> future;     // the future expression to await

        AwaitExpr(Token keyword, std::shared_ptr<Expr> future)
                : keyword(std::move(keyword)), future(std::move(future)) {}

        std::any accept(ExprVisitor& visitor) const override {
            return visitor.visit(*this);
        }
    };

    // Inline assembly expression. Lowered to llvm::InlineAsm at codegen.
    //
    //   @unsafe { asm("msr daifset, #3"); }                              // void
    //   asm("mrs $0, CurrentEL", out("=r") el -> i64);                    // output
    //   asm("add $0, $1, $2", out("=r") s, in("r") a, in("r") b);         // mixed
    //
    // Operands are positional ($0..$N); outputs are listed before inputs in
    // the constraint string, matching the LLVM/GCC inline-asm convention.
    // An `out`/`inout` operand must be an assignable lvalue (a VarExpr). A
    // `-> type` clause gives the asm's result type; with no output operands
    // the result defaults to nil. Requires an @unsafe block (E920).
    enum class AsmDir { IN, OUT, INOUT };

    struct AsmOperand {
        const AsmDir dir;
        const Token constraint;                 // the constraint STRING literal
        const std::shared_ptr<Expr> expr;       // value (in) / lvalue (out, inout)
    };

    struct AsmExpr : Expr {
        const Token keyword;                    // the 'asm' token
        const Token asmString;                  // the template STRING literal (lexeme = unescaped text)
        const std::vector<AsmOperand> operands;
        const std::shared_ptr<ASTType> resultType;  // optional "-> type" clause (nullptr = nil)

        AsmExpr(Token keyword, Token asmString,
                std::vector<AsmOperand> operands,
                std::shared_ptr<ASTType> resultType)
                : keyword(std::move(keyword)), asmString(std::move(asmString)),
                  operands(std::move(operands)), resultType(std::move(resultType)) {}

        std::any accept(ExprVisitor& visitor) const override {
            return visitor.visit(*this);
        }
    };
}
