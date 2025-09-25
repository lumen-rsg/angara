//
// Created by cv2 on 8/27/25.
//

#include "Parser.h"


namespace angara {
    Parser::Parser(const std::vector<Token> &tokens, ErrorHandler &errorHandler)
            : m_tokens(tokens), m_errorHandler(errorHandler), m_panicMode(false) {}



std::shared_ptr<ASTType> Parser::type() {
    std::shared_ptr<ASTType> base_type;

    // --- Step 1: Parse the base of the type. ---
    // This can be a record literal, a function literal, or a name-based type.

    // Check for an inline record type, e.g., `{ name: string, id: i64 }`
    if (match({TokenType::LEFT_BRACE})) {
        Token keyword = previous(); // The '{' token
        std::vector<RecordFieldType> fields;
        if (!check(TokenType::RIGHT_BRACE)) {
            do {
                if (check(TokenType::RIGHT_BRACE)) break; // Allows trailing comma
                Token field_name;
                if (match({TokenType::IDENTIFIER})) field_name = previous();
                else if (match({TokenType::STRING})) field_name = previous();
                else throw error(peek(), "Expect field name (identifier or string) in record type definition.");

                consume(TokenType::COLON, "Expect ':' after field name.");
                fields.push_back({field_name, type()}); // Recursive call for the field's type
            } while (match({TokenType::COMMA}));
        }
        consume(TokenType::RIGHT_BRACE, "Expect '}' after record type fields.");
        base_type = std::make_shared<RecordTypeExpr>(keyword, std::move(fields));

    }
    // Check for a function type, e.g., `function(i64, string) -> bool`
    else if (match({TokenType::TYPE_FUNCTION})) {
        Token keyword = previous();
        consume(TokenType::LEFT_PAREN, "Expect '(' after 'function' in type annotation.");
        std::vector<std::shared_ptr<ASTType>> params;
        if (!check(TokenType::RIGHT_PAREN)) {
            do {
                params.push_back(type());
            } while (match({TokenType::COMMA}));
        }
        consume(TokenType::RIGHT_PAREN, "Expect ')' after function type parameters.");
        consume(TokenType::MINUS_GREATER, "Expect '->' for return type.");
        auto return_type = type();
        base_type = std::make_shared<FunctionTypeExpr>(keyword, std::move(params), return_type);

    }
    // Check for any kind of name-based type (e.g., `i64`, `list`, `User`)
    else if (match({
    // Keep the existing tokens
    TokenType::IDENTIFIER, TokenType::NIL,

    // Add all keywords that can represent a type
    TokenType::TYPE_LIST, TokenType::TYPE_MAP, TokenType::TYPE_STRING,
    TokenType::TYPE_INT, TokenType::TYPE_FLOAT, TokenType::TYPE_BOOL, TokenType::TYPE_RECORD
})) {
        Token type_name_token = previous();

        // After finding a name, check if it's a generic type like `list<string>`
        if (match({TokenType::LESS})) {
            std::vector<std::shared_ptr<ASTType>> arguments;
            do {
                arguments.push_back(type()); // Recursive call for generic arguments
            } while (match({TokenType::COMMA}));
            consume(TokenType::GREATER, "Expect '>' after generic type arguments.");
            base_type = std::make_shared<GenericType>(type_name_token, std::move(arguments));
        } else {
            // If not generic, it was just a simple type name.
            base_type = std::make_shared<SimpleType>(type_name_token);
        }
    }
    // If none of the above matched, it's a syntax error.
    else {
        throw error(peek(), "Expect a type name (like 'i64' or 'User'), a function type, or a record type definition.");
    }

    // --- Step 2: After successfully parsing a base type, check for an optional suffix. ---
    if (match({TokenType::QUESTION})) {
        // If we see a '?', wrap the entire type we just parsed in an OptionalType node.
        return std::make_shared<OptionalTypeNode>(base_type);
    }

    // If there was no '?', just return the base type we parsed.
    return base_type;
}


    std::vector<std::shared_ptr<Stmt>> Parser::parseStmts() {
        std::vector<std::shared_ptr<Stmt>> statements;
        while (!isAtEnd()) {
            statements.push_back(declaration());
        }
        return statements;
    }

    std::shared_ptr<Stmt> Parser::statement() {
        if (match({TokenType::FOR})) return forStatement();
        if (match({TokenType::IF})) return ifStatement();
        if (match({TokenType::WHILE})) return whileStatement();
        if (match({TokenType::THROW})) return throwStatement();
        if (match({TokenType::RETURN})) return returnStatement();
        if (match({TokenType::LEFT_BRACE})) return std::make_shared<BlockStmt>(block());
        if (match({TokenType::SEMICOLON})) return std::make_shared<EmptyStmt>();
        if (match({TokenType::TRY})) return tryStatement();
        if (match({TokenType::BREAK})) return breakStatement();

        return expressionStatement();
    }

// expressionStatement -> expression ";"
    std::shared_ptr<Stmt> Parser::expressionStatement() {
        std::shared_ptr<Expr> expr = expression();
        consume(TokenType::SEMICOLON, "Expect ';' after expression.");
        return std::make_shared<ExpressionStmt>(std::move(expr));
    }


// expression → assignment
    std::shared_ptr<Expr> Parser::expression() {
        return assignment();
    }

    bool Parser::match(const std::vector<TokenType> &types) {
        for (const TokenType type: types) {
            if (check(type)) {
                advance();
                return true;
            }
        }
        return false;
    }

    Token Parser::consume(TokenType type, const std::string &message) {
        if (check(type)) return advance();
        throw error(peek(), message);
    }

    bool Parser::check(TokenType type) {
        if (isAtEnd()) return false;
        return peek().type == type;
    }

    Token Parser::advance() {
        if (!isAtEnd()) m_current++;
        return previous();
    }

    bool Parser::isAtEnd() {
        return peek().type == TokenType::EOF_TOKEN;
    }

    Token Parser::peek() {
        return m_tokens[m_current];
    }

    Token Parser::previous() {
        return m_tokens[m_current - 1];
    }

    Parser::ParseError Parser::error(const Token &token, const std::string &message) {
        // If we are already in panic mode, don't report another error.
        // Just keep throwing to unwind the stack until we synchronize.
        if (m_panicMode) {
            return ParseError(""); // Return a dummy error
        }

        // This is the first error. Enter panic mode and report it.
        m_panicMode = true;
        m_errorHandler.report(token, message);
        return ParseError(message);
    }

    void Parser::synchronize() {
        advance(); // Consume the token that caused the error

        while (!isAtEnd()) {
            if (previous().type == TokenType::SEMICOLON) return;

            switch (peek().type) {
                // These keywords often start a new statement, so we can stop here.
                case TokenType::FUNC:
                case TokenType::LET:
                case TokenType::FOR:
                case TokenType::IF:
                case TokenType::WHILE:
                case TokenType::RETURN:
                    return;
            }
            advance();
        }
    }




}

