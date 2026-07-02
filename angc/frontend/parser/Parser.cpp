#include "Parser.h"

namespace angara {
    Parser::Parser(const std::vector<Token> &tokens, ErrorHandler &errorHandler)
            : m_tokens(tokens), m_errorHandler(errorHandler), m_panicMode(false) {}


    std::shared_ptr<ASTType> Parser::type() {
        // Parse @own prefix for owned string types (FFI only)
        if (match({TokenType::AT_SIGN})) {
            Token own_kw = consume(TokenType::IDENTIFIER, "Expected 'own' after '@' in type annotation.", "E100");
            if (own_kw.lexeme != "own") {
                throw error(own_kw, "Only '@own' type modifier is supported.", "E101");
            }
            auto inner = type();
            return std::make_shared<OwnedTypeNode>(inner);
        }

        // Parse pointer prefix: *i8, **i8, *void
        // Parse byval prefix: ^Vec2 (struct-by-value for FFI)
        bool is_byval = match({TokenType::CARET});
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
                    else throw error(peek(), "Expected a field name (identifier or string) in record type.", "E102");

                    consume(TokenType::COLON, "Expected ':' after field name in record type.", "E103");
                    fields.push_back({field_name, type()});
                } while (match({TokenType::COMMA}));
            }
            consume(TokenType::RIGHT_BRACE, "Expected '}' after record type fields.", "E104");
            base_type = std::make_shared<RecordTypeExpr>(keyword, std::move(fields));

        }
        else if (match({TokenType::TYPE_FUNCTION})) {
            Token keyword = previous();
            consume(TokenType::LEFT_PAREN, "Expected '(' after 'function' in type annotation.", "E105");
            std::vector<std::shared_ptr<ASTType>> params;
            if (!check(TokenType::RIGHT_PAREN)) {
                do {
                    params.push_back(type());
                } while (match({TokenType::COMMA}));
            }
            consume(TokenType::RIGHT_PAREN, "Expected ')' after function type parameters.", "E106");
            consume(TokenType::MINUS_GREATER, "Expected '->' before return type in function type.", "E107");
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
                consume(TokenType::GREATER, "Expected '>' after generic type arguments.", "E108");
                base_type = std::make_shared<GenericType>(type_name_token, std::move(arguments));
            } else {
                base_type = std::make_shared<SimpleType>(type_name_token);
            }

            // Check for fixed-size array: i8[256]
            if (match({TokenType::LEFT_BRACKET})) {
                Token size_token = consume(TokenType::NUMBER_INT, "Expected array size after '['.", "E109");
                consume(TokenType::RIGHT_BRACKET, "Expected ']' after array size.", "E110");
                int arr_size = std::stoi(size_token.lexeme);
                base_type = std::make_shared<FixedArrayTypeExpr>(base_type, arr_size);
            }
        }
        else {
            throw error(peek(), "Expected a type annotation (e.g., 'i64', 'string'), a function type ('function(A) -> B'), or a record type ('{ field: Type }').", "E111");
        }

        if (match({TokenType::QUESTION})) {
            base_type = std::make_shared<OptionalTypeNode>(base_type);
        }

        // Wrap in pointer type if * prefix was parsed
        if (ptr_depth > 0 || is_byval) {
            auto ptr_type = std::make_shared<PointerTypeExpr>(base_type, ptr_depth);
            ptr_type->byval = is_byval;
            base_type = ptr_type;
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
        if (match({TokenType::DROP})) return dropStatement();
        if (match({TokenType::RETURN})) return returnStatement();
        if (match({TokenType::LEFT_BRACE})) return std::make_shared<BlockStmt>(block());
        if (match({TokenType::SEMICOLON})) return std::make_shared<EmptyStmt>();
        if (match({TokenType::TRY})) return tryStatement();
        if (match({TokenType::BREAK})) return breakStatement();
        if (match({TokenType::CONTINUE})) return continueStatement();
        if (match({TokenType::AT_SIGN})) {
            Token at_token = previous();
            Token annotation = consume(TokenType::IDENTIFIER, "Expected annotation name after '@'.", "E112");
            if (annotation.lexeme != "unsafe") {
                throw error(annotation, "Unknown annotation '@" + annotation.lexeme + "'. Only '@unsafe' is supported.", "E113");
            }

            consume(TokenType::LEFT_BRACE, "Expected '{' to begin '@unsafe' block.", "E114");
            auto block_node = std::make_shared<BlockStmt>(block());
            return std::make_shared<UnsafeBlockStmt>(at_token, block_node);
        }

        return expressionStatement();
    }

    std::shared_ptr<Stmt> Parser::expressionStatement() {
        std::shared_ptr<Expr> expr = expression();
        consume(TokenType::SEMICOLON, "Expected ';' after expression statement.", "E115");
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

    Token Parser::consume(TokenType type, const std::string &message, const std::string &code) {
        if (check(type)) return advance();

        // If expecting an identifier but found a keyword, give a clearer error
        if (type == TokenType::IDENTIFIER && isKeywordToken(peek().type)) {
            throw error(peek(),
                "Cannot use reserved keyword '" + peek().lexeme + "' as an identifier. "
                "Reserved keywords cannot be used as variable, field, or parameter names.",
                code);
        }

        throw error(peek(), message, code);
    }

    bool Parser::isKeywordToken(TokenType type) {
        switch (type) {
            case TokenType::LET: case TokenType::CONST: case TokenType::IF:
            case TokenType::ELSE: case TokenType::ORIF: case TokenType::FOR:
            case TokenType::WHILE: case TokenType::IN: case TokenType::FUNC:
            case TokenType::RETURN: case TokenType::TRUE: case TokenType::FALSE:
            case TokenType::TRY: case TokenType::CATCH: case TokenType::ATTACH:
            case TokenType::NIL: case TokenType::THROW: case TokenType::FROM:
            case TokenType::CLASS: case TokenType::THIS: case TokenType::INHERITS:
            case TokenType::SUPER: case TokenType::TRAIT: case TokenType::USES:
            case TokenType::STATIC: case TokenType::PRIVATE: case TokenType::PUBLIC:
            case TokenType::EXPORT: case TokenType::CONTRACT: case TokenType::SIGNS:
            case TokenType::BREAK: case TokenType::CONTINUE: case TokenType::IS:
            case TokenType::DATA: case TokenType::ENUM: case TokenType::MATCH: case TokenType::DROP: case TokenType::OWNED:
            case TokenType::CASE: case TokenType::FOREIGN: case TokenType::INTRINSIC:
            case TokenType::UNION: case TokenType::AS:
            case TokenType::TYPE_STRING: case TokenType::TYPE_INT:
            case TokenType::TYPE_FLOAT: case TokenType::TYPE_BOOL:
            case TokenType::TYPE_LIST: case TokenType::TYPE_MAP: case TokenType::TYPE_VOID:
            case TokenType::TYPE_I8: case TokenType::TYPE_I16: case TokenType::TYPE_I32:
            case TokenType::TYPE_I64: case TokenType::TYPE_U8: case TokenType::TYPE_U16:
            case TokenType::TYPE_U32: case TokenType::TYPE_U64: case TokenType::TYPE_UINT:
            case TokenType::TYPE_F32: case TokenType::TYPE_F64: case TokenType::TYPE_NIL:
            case TokenType::TYPE_RECORD: case TokenType::TYPE_FUNCTION:
            case TokenType::TYPE_ANY: case TokenType::TYPE_THREAD:
                return true;
            default:
                return false;
        }
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

    Parser::ParseError Parser::error(const Token &token, const std::string &message, const std::string &code) {
        if (m_panicMode) {
            return ParseError("");
        }

        m_panicMode = true;
        m_errorHandler.report(token, message, code);
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
