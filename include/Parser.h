//
// Created by cv2 on 8/27/25.
//

#pragma once

#include <vector>
#include <memory>
#include "Token.h"
#include "Expr.h"
#include "Stmt.h"
#include "ErrorHandler.h"

namespace angara {

    class Parser {
    public:
        Parser(const std::vector<Token> &tokens, ErrorHandler &errorHandler);

        std::vector<std::shared_ptr<Stmt>> parseStmts();

    private:
        std::shared_ptr<ASTType> type();
        std::shared_ptr<Stmt> declaration();
        std::shared_ptr<Stmt> varDeclaration(bool is_const);
        std::shared_ptr<Stmt> statement();
        std::shared_ptr<Stmt> expressionStatement();
        std::shared_ptr<Stmt> ifStatement();
        std::shared_ptr<Stmt> forStatement();
        std::vector<std::shared_ptr<Stmt>> block();
        std::shared_ptr<Expr> assignment();
        std::shared_ptr<Stmt> parseCStyleLoop(const Token& keyword);
        std::shared_ptr<Stmt> parseForInLoop(const Token& keyword);
        std::shared_ptr<Stmt> whileStatement();

        bool isForInLoop();
        std::shared_ptr<Stmt> function(const std::string &kind); // Helper for functions/methods
        std::shared_ptr<Stmt> returnStatement();

        // Grammar rule methods
        std::shared_ptr<Expr> expression();
        std::shared_ptr<Expr> equality();
        std::shared_ptr<Expr> comparison();
        std::shared_ptr<Expr> term();
        std::shared_ptr<Expr> factor();
        std::shared_ptr<Expr> unary();
        std::shared_ptr<Expr> primary();
        std::shared_ptr<Expr> call();

        // Helper methods
        bool match(const std::vector<TokenType> &types);
        Token consume(TokenType type, const std::string &message);
        bool check(TokenType type);
        Token advance();
        bool isAtEnd();
        Token peek();
        Token previous();

        // Error handling
        class ParseError : public std::runtime_error {
        public:
            using std::runtime_error::runtime_error;
        };

        ParseError error(const Token &token, const std::string &message);

        void synchronize();

        const std::vector<Token> &m_tokens;
        int m_current = 0;
        ErrorHandler &m_errorHandler;
        bool m_panicMode = false;

        std::shared_ptr<Stmt> attachStatement();
        std::shared_ptr<Expr> logic_or();
        std::shared_ptr<Expr> logic_and();
        std::shared_ptr<Stmt> traitDeclaration();

        std::shared_ptr<Stmt> contractDeclaration();

        std::shared_ptr<Stmt> throwStatement();
        std::shared_ptr<Stmt> tryStatement();
        std::shared_ptr<Expr> ternary();
        std::shared_ptr<Expr> nil_coalescing();

        bool isSelectiveAttach();
        std::shared_ptr<Stmt> classDeclaration();

        std::shared_ptr<Stmt> breakStatement();

        std::shared_ptr<Stmt> dataDeclaration();
    };
}