#include "Parser.h"

namespace angara {
    Parser::Parser(const std::vector<Token> &tokens, ErrorHandler &errorHandler)
            : m_tokens(tokens), m_errorHandler(errorHandler), m_panicMode(false) {}


    std::shared_ptr<ASTType> Parser::type() {
        // Parse @own prefix for owned string types (FFI only)
        if (match({TokenType::AT_SIGN})) {
            Token own_kw = consume(TokenType::IDENTIFIER, "Expected 'own' after '@' in type annotation.");
            if (own_kw.lexeme != "own") {
                throw error(own_kw, "Only '@own' type modifier is supported.");
            }
            auto inner = type();
            return std::make_shared<OwnedTypeNode>(inner);
        }

        // Parse pointer prefix: *i8, **i8, *void
        int ptr_depth = 0;
        while (match({TokenType::STAR})) {
            ptr_depth++;
        }

        std::shared_ptr<ASTType> base_type;

        if (match({TokenType::LEFT_BRACE})) {
            Token keyword = previous();
            std::vector<RecordFieldType> fields;
            if (!check(TokenType::RIGHT_BRACE)) {
                do {
                    if (check(TokenType::RIGHT_BRACE)) break;
                    Token field_name;
                    if (match({TokenType::IDENTIFIER})) field_name = previous();
                    else if (match({TokenType::STRING})) field_name = previous();
                    else throw error(peek(), "Expected a field name (identifier or string) in record type.");

                    consume(TokenType::COLON, "Expected ':' after field name in record type.");
                    fields.push_back({field_name, type()});
                } while (match({TokenType::COMMA}));
            }
            consume(TokenType::RIGHT_BRACE, "Expected '}' after record type fields.");
            base_type = std::make_shared<RecordTypeExpr>(keyword, std::move(fields));

        }
        else if (match({TokenType::TYPE_FUNCTION})) {
            Token keyword = previous();
            consume(TokenType::LEFT_PAREN, "Expected '(' after 'function' in type annotation.");
            std::vector<std::shared_ptr<ASTType>> params;
            if (!check(TokenType::RIGHT_PAREN)) {
                do {
                    params.push_back(type());
                } while (match({TokenType::COMMA}));
            }
            consume(TokenType::RIGHT_PAREN, "Expected ')' after function type parameters.");
            consume(TokenType::MINUS_GREATER, "Expected '->' before return type in function type.");
            auto return_type = type();
            base_type = std::make_shared<FunctionTypeExpr>(keyword, std::move(params), return_type);

        }
        else if (match({
            TokenType::IDENTIFIER, TokenType::NIL,
            TokenType::TYPE_STRING,
            TokenType::TYPE_INT, TokenType::TYPE_FLOAT, TokenType::TYPE_BOOL,
            TokenType::TYPE_VOID
        })) {
            Token type_name_token = previous();

            if (match({TokenType::LESS})) {
                std::vector<std::shared_ptr<ASTType>> arguments;
                do {
                    arguments.push_back(type());
                } while (match({TokenType::COMMA}));
                consume(TokenType::GREATER, "Expected '>' after generic type arguments.");
                base_type = std::make_shared<GenericType>(type_name_token, std::move(arguments));
            } else {
                base_type = std::make_shared<SimpleType>(type_name_token);
            }

            // Check for fixed-size array: i8[256]
            if (match({TokenType::LEFT_BRACKET})) {
                Token size_token = consume(TokenType::NUMBER_INT, "Expected array size after '['.");
                consume(TokenType::RIGHT_BRACKET, "Expected ']' after array size.");
                int arr_size = std::stoi(size_token.lexeme);
                base_type = std::make_shared<FixedArrayTypeExpr>(base_type, arr_size);
            }
        }
        else {
            throw error(peek(), "Expected a type annotation (e.g., 'i64', 'string'), a function type ('function(A) -> B'), or a record type ('{ field: Type }').");
        }

        if (match({TokenType::QUESTION})) {
            base_type = std::make_shared<OptionalTypeNode>(base_type);
        }

        // Wrap in pointer type if * prefix was parsed
        if (ptr_depth > 0) {
            base_type = std::make_shared<PointerTypeExpr>(base_type, ptr_depth);
        }

        return base_type;
    }


    std::vector<std::shared_ptr<Stmt>> Parser::parseStmts() {
        std::vector<std::shared_ptr<Stmt>> statements;
        while (!isAtEnd()) {
            auto stmt = declaration();
            if (stmt) statements.push_back(std::move(stmt));
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
        if (match({TokenType::CONTINUE})) return continueStatement();
        if (match({TokenType::AT_SIGN})) {
            Token at_token = previous();
            Token annotation = consume(TokenType::IDENTIFIER, "Expected annotation name after '@'.");
            if (annotation.lexeme != "unsafe") {
                throw error(annotation, "Unknown annotation '@" + annotation.lexeme + "'. Only '@unsafe' is supported.");
            }

            consume(TokenType::LEFT_BRACE, "Expected '{' to begin '@unsafe' block.");
            auto block_node = std::make_shared<BlockStmt>(block());
            return std::make_shared<UnsafeBlockStmt>(at_token, block_node);
        }

        return expressionStatement();
    }

    std::shared_ptr<Stmt> Parser::expressionStatement() {
        std::shared_ptr<Expr> expr = expression();
        consume(TokenType::SEMICOLON, "Expected ';' after expression statement.");
        return std::make_shared<ExpressionStmt>(std::move(expr));
    }

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

    bool Parser::isAtEnd() const {
        return peek().type == TokenType::EOF_TOKEN;
    }

    Token Parser::peek() const {
        return m_tokens[m_current];
    }

    Token Parser::previous() const {
        return m_tokens[m_current - 1];
    }

    Parser::ParseError Parser::error(const Token &token, const std::string &message) {
        if (m_panicMode) {
            return ParseError("");
        }

        m_panicMode = true;
        m_errorHandler.report(token, message);
        return ParseError(message);
    }

    void Parser::warning(const Token &token, const std::string &message) {
        m_errorHandler.warning(token, message);
    }

    void Parser::synchronize() {
        advance();

        while (!isAtEnd()) {
            if (previous().type == TokenType::SEMICOLON) return;

            switch (peek().type) {
                case TokenType::FUNC:
                case TokenType::LET:
                case TokenType::FOR:
                case TokenType::IF:
                case TokenType::WHILE:
                case TokenType::RETURN:
                    return;
                default: ;
            }
            advance();
        }
    }

    std::vector<Token> Parser::parseTypeParams() {
        if (!check(TokenType::LESS)) {
            return {};
        }

        int saved = m_current;

        advance();

        if (!check(TokenType::IDENTIFIER)) {
            m_current = saved;
            return {};
        }

        std::vector<Token> params;

        while (true) {
            if (!check(TokenType::IDENTIFIER)) {
                m_current = saved;
                return {};
            }

            params.push_back(advance());

            if (check(TokenType::GREATER)) {
                advance();
                return params;
            }

            if (!check(TokenType::COMMA)) {
                m_current = saved;
                return {};
            }

            advance();
        }
    }

}
