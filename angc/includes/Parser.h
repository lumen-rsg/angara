#pragma once

#include <vector>
#include <memory>
#include "Token.h"
#include "Expr.h"
#include "Stmt.h"
#include "ErrorHandler.h"

namespace angara {

    /// Recursive-descent parser that converts a flat Token stream into an AST.
    class Parser {
    public:
        /// Constructs a Parser over the given token list.
        /// @param tokens        Token stream produced by the Lexer (must end with EOF_TOKEN).
        /// @param errorHandler  Error reporter used for diagnostics.
        Parser(const std::vector<Token> &tokens, ErrorHandler &errorHandler);

        /// Parses the entire token stream and returns a list of top-level statements.
        /// @return Ordered vector of AST statement nodes.
        std::vector<std::shared_ptr<Stmt>> parseStmts();

    private:
        /// Parses a type annotation (simple name, generic, function type, record type, or optional).
        /// @return Shared pointer to the parsed AST type node.
        std::shared_ptr<ASTType> type();

        /// Top-level dispatch: tries to parse a declaration, falls back to a statement.
        /// @return The parsed statement node.
        std::shared_ptr<Stmt> declaration();

        /// Parses a variable declaration (`let` or `const`).
        /// @param is_const  True if the variable is declared as immutable.
        /// @return The parsed variable declaration node.
        std::shared_ptr<Stmt> varDeclaration(bool is_const);

        /// Dispatches to the appropriate statement parser based on the current token.
        /// @return The parsed statement node.
        std::shared_ptr<Stmt> statement();

        /// Parses an expression followed by a semicolon.
        /// @return The parsed expression-statement node.
        std::shared_ptr<Stmt> expressionStatement();

        /// Parses an `if` / `orif` / `else` chain.
        /// @return The parsed if-statement node.
        std::shared_ptr<Stmt> ifStatement();

        /// Dispatches to C-style or for-in loop parsing based on lookahead.
        /// @return The parsed for-statement node.
        std::shared_ptr<Stmt> forStatement();

        /// Parses a brace-delimited block of statements.
        /// @return Ordered vector of statements inside the block.
        std::vector<std::shared_ptr<Stmt>> block();

        /// Parses an assignment (or falls through to lower-precedence expressions).
        /// @return The parsed expression node.
        std::shared_ptr<Expr> assignment();

        /// Parses a C-style `for (init; cond; update) { ... }` loop.
        /// @param keyword  The `for` token (for error reporting).
        /// @return The parsed for-statement node.
        std::shared_ptr<Stmt> parseCStyleLoop(const Token& keyword);

        /// Parses a for-in loop: `for (name in iterable) { ... }`.
        /// @param keyword  The `for` token (for error reporting).
        /// @return The parsed for-in-statement node.
        std::shared_ptr<Stmt> parseForInLoop(const Token& keyword);

        /// Parses a `while (cond) { ... }` loop.
        /// @return The parsed while-statement node.
        std::shared_ptr<Stmt> whileStatement();

        /// Parses a `match` expression with `case` arms.
        /// @return The parsed match expression node.
        std::shared_ptr<Expr> matchExpression();

        /// Lookahead check to determine if the current `for` is a for-in loop.
        /// @return True if the for-loop uses `in` syntax.
        bool isForInLoop();

        /// Parses a function or method declaration.
        /// @param kind  Label for error messages (e.g., "function" or "method").
        /// @return The parsed function declaration node.
        std::shared_ptr<Stmt> function(const std::string &kind);

        /// Parses a `return` statement.
        /// @return The parsed return-statement node.
        std::shared_ptr<Stmt> returnStatement();

        /// Parses an `attach` statement for extending existing types.
        /// @return The parsed attach-statement node.
        std::shared_ptr<Stmt> attachStatement();

        /// Parses a `trait` declaration.
        /// @return The parsed trait-declaration node.
        std::shared_ptr<Stmt> traitDeclaration();

        /// Parses a `contract` declaration.
        /// @return The parsed contract-declaration node.
        std::shared_ptr<Stmt> contractDeclaration();

        /// Parses a `throw` statement.
        /// @return The parsed throw-statement node.
        std::shared_ptr<Stmt> throwStatement();

        /// Parses a `try` / `catch` statement.
        /// @return The parsed try-statement node.
        std::shared_ptr<Stmt> tryStatement();

        /// Parses a `class` declaration with optional `inherits` clause.
        /// @return The parsed class-declaration node.
        std::shared_ptr<Stmt> classDeclaration();

        /// Parses a `break` statement.
        /// @return The parsed break-statement node.
        std::shared_ptr<Stmt> breakStatement();

        /// Parses a `continue` statement.
        /// @return The parsed continue-statement node.
        std::shared_ptr<Stmt> continueStatement();

        /// Parses a `data` declaration (algebraic data type / struct).
        /// @return The parsed data-declaration node.
        std::shared_ptr<Stmt> dataDeclaration();

        /// Parses a `foreign data` declaration (C-compatible struct or opaque handle).
        std::shared_ptr<Stmt> foreignDataDeclaration();

        /// Parses an `enum` declaration.
        /// @return The parsed enum-declaration node.
        std::shared_ptr<Stmt> enumDeclaration();

        /// Parses a single pattern inside a `case` arm of a `match` expression.
        /// @return The parsed pattern expression node.
        std::shared_ptr<Expr> parseMatchPattern();

        /// Parses a generic type-parameter list `<T, U, V>` after a declaration name.
        /// Uses lookahead to disambiguate from a comparison expression.
        /// @return Vector of type-parameter tokens, or empty if none found.
        std::vector<Token> parseTypeParams();

        /// Parses an anonymous function (lambda) expression after the `func` keyword.
        /// @param keyword  The `func` token (for error reporting).
        /// @return The parsed lambda expression node.
        std::shared_ptr<Expr> lambdaExpression(const Token& keyword);

        // --- Expression precedence levels (lowest to highest) ---

        /// Entry point for expression parsing; delegates to assignment.
        /// @return The parsed expression node.
        std::shared_ptr<Expr> expression();

        /// Parses `or` expressions (logical disjunction).
        /// @return The parsed expression node.
        std::shared_ptr<Expr> logic_or();

        /// Parses `and` expressions (logical conjunction).
        /// @return The parsed expression node.
        std::shared_ptr<Expr> logic_and();

        /// Parses `==` / `!=` expressions.
        /// @return The parsed expression node.
        std::shared_ptr<Expr> equality();

        /// Parses `<`, `<=`, `>`, `>=` expressions.
        /// @return The parsed expression node.
        std::shared_ptr<Expr> comparison();

        /// Parses bitwise `|`, `&`, `^`, `~` expressions.
        /// @return The parsed expression node.
        std::shared_ptr<Expr> bitwise();

        /// Parses `+`, `-` expressions.
        /// @return The parsed expression node.
        std::shared_ptr<Expr> term();

        /// Parses `*`, `/`, `%` expressions.
        /// @return The parsed expression node.
        std::shared_ptr<Expr> factor();

        /// Parses unary prefix operators (`!`, `-`).
        /// @return The parsed expression node.
        std::shared_ptr<Expr> unary();

        /// Parses ternary `cond ? then : else` expressions.
        /// @return The parsed expression node.
        std::shared_ptr<Expr> ternary();

        /// Parses nil-coalescing `??` expressions.
        /// @return The parsed expression node.
        std::shared_ptr<Expr> nil_coalescing();

        /// Parses call expressions, field access, subscripts, and other postfix operators.
        /// @return The parsed expression node.
        std::shared_ptr<Expr> call();

        /// Parses primary expressions: literals, identifiers, parenthesized expressions, etc.
        /// @return The parsed expression node.
        std::shared_ptr<Expr> primary();

        // --- Token-stream helpers ---

        /// Checks if the current token matches any of the given types and advances if so.
        /// @param types  List of token types to match.
        /// @return True if a match was found and consumed.
        bool match(const std::vector<TokenType> &types);

        /// Consumes the current token if it matches the expected type; otherwise reports an error.
        /// @param type     Expected token type.
        /// @param message  Error message if the token doesn't match.
        /// @return The consumed token.
        Token consume(TokenType type, const std::string &message);

        /// Checks if the current token is of the given type without consuming it.
        /// @param type  Token type to check.
        /// @return True if the current token matches.
        bool check(TokenType type);

        /// Advances the cursor by one and returns the previous token.
        /// @return The token that was just consumed.
        Token advance();

        /// Checks if the parser has reached the end of the token stream.
        /// @return True if at EOF.
        bool isAtEnd() const;

        /// Returns the current token without consuming it.
        /// @return The token at the current cursor position.
        Token peek() const;

        /// Returns the most recently consumed token.
        /// @return The token immediately before the current cursor position.
        Token previous() const;

        /// Lookahead to determine if the current `attach` is selective.
        /// @return True if the attach targets a specific function.
        bool isSelectiveAttach();

        // --- Error handling ---

        class ParseError : public std::runtime_error {
        public:
            using std::runtime_error::runtime_error;
        };

        /// Reports a parse error at the given token and enters panic mode.
        /// @param token    Token where the error occurred.
        /// @param message  Human-readable error description.
        /// @return A ParseError exception (caller should throw it).
        ParseError error(const Token &token, const std::string &message);

        /// Reports a non-fatal warning at the given token (does not trigger panic mode).
        /// @param token    Token where the warning occurred.
        /// @param message  Human-readable warning description.
        void warning(const Token &token, const std::string &message);

        /// Discards tokens until a statement boundary is found, allowing the parser to resume.
        void synchronize();

        const std::vector<Token> &m_tokens;
        int m_current = 0;
        ErrorHandler &m_errorHandler;
        bool m_panicMode = false;
    };
}
