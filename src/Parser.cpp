//
// Created by cv2 on 8/27/25.
//

#include "Parser.h"


namespace angara {
    Parser::Parser(const std::vector<Token> &tokens, ErrorHandler &errorHandler)
            : m_tokens(tokens), m_errorHandler(errorHandler), m_panicMode(false) {}


    std::shared_ptr<ASTType> Parser::type() {
        // --- Step 1: Define all possible tokens that can start a named type ---
        // This includes user-defined types (IDENTIFIER) and all built-in type names.
        const static std::vector<TokenType> name_based_type_starters = {
                TokenType::IDENTIFIER, TokenType::TYPE_STRING, TokenType::TYPE_INT,
                TokenType::TYPE_FLOAT, TokenType::TYPE_BOOL, TokenType::TYPE_LIST,
                TokenType::TYPE_MAP, TokenType::TYPE_I8,
                TokenType::TYPE_I16, TokenType::TYPE_I32, TokenType::TYPE_I64,
                TokenType::TYPE_U8, TokenType::TYPE_U16, TokenType::TYPE_U32,
                TokenType::TYPE_U64, TokenType::TYPE_UINT, TokenType::TYPE_F32,
                TokenType::TYPE_F64, TokenType::NIL, TokenType::TYPE_ANY,
                TokenType::TYPE_THREAD
        };

        // --- Step 2: Check for complex, token-based types first ---
        if (match({TokenType::LEFT_BRACE})) {
            Token keyword = previous();
            std::vector<RecordFieldType> fields;
            if (!check(TokenType::RIGHT_BRACE)) {
                do {
                    if (check(TokenType::RIGHT_BRACE)) break;
                    Token field_name;
                    if (match({TokenType::IDENTIFIER})) field_name = previous();
                    else if (match({TokenType::STRING})) field_name = previous();
                    else throw error(peek(), "Expect field name (identifier or string) in record type definition.");
                    consume(TokenType::COLON, "Expect ':' after field name.");
                    fields.push_back({field_name, type()});
                } while (match({TokenType::COMMA}));
            }
            consume(TokenType::RIGHT_BRACE, "Expect '}' after record type fields.");
            return std::make_shared<RecordTypeExpr>(keyword, std::move(fields));
        }

        if (match({TokenType::TYPE_FUNCTION})) {
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
            return std::make_shared<FunctionTypeExpr>(keyword, std::move(params), return_type);
        }

        // --- Step 3: Check for any kind of name-based type ---
        if (match(name_based_type_starters)) {
            Token type_name_token = previous();

            // --- Step 3a: Check if it's a generic type ---
            if (match({TokenType::LESS})) {
                std::vector<std::shared_ptr<ASTType>> arguments;
                do {
                    arguments.push_back(type());
                } while (match({TokenType::COMMA}));
                consume(TokenType::GREATER, "Expect '>' after generic type arguments.");
                return std::make_shared<GenericType>(type_name_token, std::move(arguments));
            }

            // --- Step 3b: Otherwise, it was just a simple type ---
            return std::make_shared<SimpleType>(type_name_token);
        }

        // --- Step 4: If nothing matched, it's an error ---
        throw error(peek(), "Expect a type name (like 'i64' or 'list'), a function type, or a record type definition.");
    }


    std::vector<std::shared_ptr<Stmt>> Parser::parseStmts() {
        std::vector<std::shared_ptr<Stmt>> statements;
        while (!isAtEnd()) {
            statements.push_back(declaration());
        }
        return statements;
    }

// declaration → letDecl | statement
    // declaration → "export"? (class_decl | trait_decl | func_decl | var_decl) | statement
std::shared_ptr<Stmt> Parser::declaration() {
    try {
        // Look for an optional 'export' keyword first.
        bool is_exported = match({TokenType::EXPORT});

        // Now, check for the actual declaration type.
        // We handle `func` separately as it can appear with or without `export`.
        if (match({TokenType::FUNC})) {
            auto func_decl = std::static_pointer_cast<FuncStmt>(function("function"));
            func_decl->is_exported = is_exported;
            return func_decl;
        }

        std::shared_ptr<Stmt> decl_stmt = nullptr;
        if (match({TokenType::CONTRACT})) {
            decl_stmt = contractDeclaration();
            // We need to downcast to set the flag.
            std::static_pointer_cast<ContractStmt>(decl_stmt)->is_exported = is_exported;
        } else if (match({TokenType::CLASS})) {
            decl_stmt = classDeclaration();
            std::static_pointer_cast<ClassStmt>(decl_stmt)->is_exported = is_exported;
        } else if (match({TokenType::TRAIT})) {
            decl_stmt = traitDeclaration();
            std::static_pointer_cast<TraitStmt>(decl_stmt)->is_exported = is_exported;
        } else if (match({TokenType::CONST})) {
            decl_stmt = varDeclaration(true);
            std::static_pointer_cast<VarDeclStmt>(decl_stmt)->is_exported = is_exported;
        } else if (match({TokenType::LET})) {
            decl_stmt = varDeclaration(false);
            std::static_pointer_cast<VarDeclStmt>(decl_stmt)->is_exported = is_exported;
        } else if (match({TokenType::ATTACH})) {
            // 'attach' cannot be exported, so if 'is_exported' is true, it's an error.
            if (is_exported) {
                throw error(previous(), "'attach' statements cannot be exported.");
            }
            return attachStatement();
        } else {
            // If we saw 'export' but not a valid declaration that can follow it, it's an error.
            if (is_exported) {
                throw error(peek(), "Expect a class, contract, trait, function, or variable declaration after 'export'.");
            }
            // Otherwise, it's just a regular statement.
            return statement();
        }

        return decl_stmt;

    } catch (const ParseError& error) {
        synchronize();
        return nullptr;
    }
}

// letDecl → "let" IDENTIFIER as <type> ( "=" expression )? ";"
    std::shared_ptr<Stmt> Parser::varDeclaration(bool is_const) {
        Token name = consume(TokenType::IDENTIFIER, "Expect variable name.");

        std::shared_ptr<ASTType> typeAnnotation = nullptr;
        if (match({TokenType::AS})) {
            typeAnnotation = type();
        }

        std::shared_ptr<Expr> initializer = nullptr;
        if (match({TokenType::EQUAL})) {
            initializer = expression();
        }

        if (is_const && !initializer) {
            throw error(previous(), "A 'const' variable must be initialized.");
        }

        consume(TokenType::SEMICOLON, "Expect ';' after variable declaration.");

        // Pass the is_const flag to the updated constructor
        return std::make_shared<VarDeclStmt>(std::move(name), typeAnnotation, std::move(initializer), is_const);
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

// equality → comparison ( ( "!=" | "==" ) comparison )*
    std::shared_ptr<Expr> Parser::equality() {
        // 1. Parse the left-hand side expression (a 'comparison' or higher precedence).
        std::shared_ptr<Expr> expr = comparison();

        // 2. Loop as long as we find an equality-level operator.
        while (match({TokenType::BANG_EQUAL, TokenType::EQUAL_EQUAL, TokenType::IS})) {
            Token op = previous();

            // --- THE NEW LOGIC ---
            if (op.type == TokenType::IS) {
                // If the operator is 'is', the right-hand side is a TYPE, not an expression.
                std::shared_ptr<ASTType> type_rhs = type();
                expr = std::make_shared<IsExpr>(std::move(expr), std::move(op), std::move(type_rhs));
            } else {
                // Otherwise, it's a regular binary comparison, and the RHS is an expression.
                std::shared_ptr<Expr> right = comparison();
                expr = std::make_shared<Binary>(std::move(expr), std::move(op), std::move(right));
            }
        }
        return expr;
    }

// comparison → term ( ( ">" | ">=" | "<" | "<=" ) term )*
    std::shared_ptr<Expr> Parser::comparison() {
        std::shared_ptr<Expr> expr = term();
        while (match({TokenType::GREATER, TokenType::GREATER_EQUAL, TokenType::LESS, TokenType::LESS_EQUAL})) {
            Token op = previous();
            std::shared_ptr<Expr> right = term();
            expr = std::make_shared<Binary>(std::move(expr), std::move(op), std::move(right));
        }
        return expr;
    }

// term → factor ( ( "-" | "+" ) factor )*
    std::shared_ptr<Expr> Parser::term() {
        std::shared_ptr<Expr> expr = factor();
        while (match({TokenType::MINUS, TokenType::PLUS})) {
            Token op = previous();
            std::shared_ptr<Expr> right = factor();
            expr = std::make_shared<Binary>(std::move(expr), std::move(op), std::move(right));
        }
        return expr;
    }

// factor → unary ( ( "/" | "*" | "%" ) unary )*
    std::shared_ptr<Expr> Parser::factor() {
        std::shared_ptr<Expr> expr = unary();

        // Add TokenType::PERCENT to this list.
        while (match({TokenType::SLASH, TokenType::STAR, TokenType::PERCENT})) {
            Token op = previous();
            std::shared_ptr<Expr> right = unary();
            expr = std::make_shared<Binary>(std::move(expr), std::move(op), std::move(right));
        }
        return expr;
    }

// unary → ( "!" | "-" ) unary | primary
    std::shared_ptr<Expr> Parser::unary() {
        if (match({TokenType::BANG, TokenType::MINUS, TokenType::PLUS_PLUS, TokenType::MINUS_MINUS})) {
            Token op = previous();
            std::shared_ptr<Expr> right = unary();
            // Check if it's an update operator
            if (op.type == TokenType::PLUS_PLUS || op.type == TokenType::MINUS_MINUS) {
                return std::make_shared<UpdateExpr>(std::move(right), std::move(op), true /* isPrefix */);
            }
            return std::make_shared<Unary>(std::move(op), std::move(right));
        }
        return call();
    }

    std::shared_ptr<Expr> Parser::call() {
        // First, parse the primary expression (e.g., the variable name 'sys').
        std::shared_ptr<Expr> expr = primary();

        // Now, loop as long as we find a '(', '.', '++', or '--'.
        // This allows for chaining like: getModule().member++.
        while (true) {
            if (match({TokenType::LEFT_PAREN})) {
                // --- It's a Function Call ---
                std::vector<std::shared_ptr<Expr>> arguments;
                if (!check(TokenType::RIGHT_PAREN)) {
                    do {
                        if (arguments.size() >= 255) {
                            error(peek(), "Can't have more than 255 arguments.");
                        }
                        arguments.push_back(expression());
                    } while (match({TokenType::COMMA}));
                }
                Token paren = consume(TokenType::RIGHT_PAREN, "Expect ')' after arguments.");
                // The 'expr' from the previous step becomes the "callee" of the new CallExpr.
                expr = std::make_shared<CallExpr>(std::move(expr), std::move(paren), std::move(arguments));

            } else if (match({TokenType::LEFT_BRACKET})) {
                Token bracket = previous();
                std::shared_ptr<Expr> index = expression();
                consume(TokenType::RIGHT_BRACKET, "Expect ']' after subscript index.");
                expr = std::make_shared<SubscriptExpr>(std::move(expr), std::move(bracket), std::move(index));
            } else if (match({TokenType::PLUS_PLUS, TokenType::MINUS_MINUS})) {
                // --- It's a Postfix Update ---
                Token op = previous();
                expr = std::make_shared<UpdateExpr>(std::move(expr), std::move(op), false /* isPrefix */);

            } else if (match({TokenType::DOT})) {
                Token name = consume(TokenType::IDENTIFIER, "Expect property name after '.'.");
                expr = std::make_shared<GetExpr>(std::move(expr), std::move(name));
            } else {
                // If we don't find any of the above, we're done.
                break;
            }
        }

        return expr;
    }

// primary → NUMBER | STRING | "true" | "false" | IDENTIFIER | "(" expression ")"
    std::shared_ptr<Expr> Parser::primary() {
        if (match({TokenType::FALSE, TokenType::TRUE, TokenType::NIL,
                   TokenType::NUMBER_INT, TokenType::NUMBER_FLOAT, TokenType::STRING})) {
            return std::make_shared<Literal>(previous());
        }


        if (match({TokenType::THIS})) {
            return std::make_shared<ThisExpr>(previous());
        }

        if (match({TokenType::SUPER})) {
            Token super_keyword = previous(); // This is the token for "super".
            consume(TokenType::DOT, "Expect '.' after 'super'.");
            Token method = consume(TokenType::IDENTIFIER, "Expect superclass method name.");
            Token this_keyword = Token(TokenType::THIS, "this", super_keyword.line, super_keyword.column);

            return std::make_shared<SuperExpr>(this_keyword, method);
        }

        if (match({TokenType::IDENTIFIER})) {
            return std::make_shared<VarExpr>(previous());
        }

        // List literals
        if (match({TokenType::LEFT_BRACKET})) {
            Token bracket = previous();
            std::vector<std::shared_ptr<Expr>> elements;
            if (!check(TokenType::RIGHT_BRACKET)) {
                do {
                    if (check(TokenType::RIGHT_BRACKET)) break;
                    elements.push_back(expression());
                } while (match({TokenType::COMMA}));
            }
            consume(TokenType::RIGHT_BRACKET, "Expect ']' after list elements.");
            return std::make_shared<ListExpr>(std::move(bracket), std::move(elements));
        }

        // Record literals
        if (match({TokenType::LEFT_BRACE})) {
            std::vector<Token> keys;
            std::vector<std::shared_ptr<Expr>> values;
            if (!check(TokenType::RIGHT_BRACE)) {
                do {
                    if (check(TokenType::RIGHT_BRACE)) break; // Allow trailing comma

                    Token key;
                    if (match({TokenType::STRING})) {
                        key = previous();
                    } else if (match({TokenType::IDENTIFIER})) {
                        key = previous();
                        key.type = TokenType::STRING;
                    } else {
                        throw error(peek(), "Expect string or identifier for record key.");
                    }
                    keys.push_back(key);

                    consume(TokenType::COLON, "Expect ':' after key in record literal.");
                    values.push_back(expression());
                } while (match({TokenType::COMMA}));
            }
            consume(TokenType::RIGHT_BRACE, "Expect '}' after record fields.");
            return std::make_shared<RecordExpr>(std::move(keys), std::move(values));
        }

        // Grouping
        if (match({TokenType::LEFT_PAREN})) {
            std::shared_ptr<Expr> expr = expression();
            consume(TokenType::RIGHT_PAREN, "Expect ')' after expression.");
            return std::make_shared<Grouping>(std::move(expr));
        }

        // If none of the above matched, it's an error.
        throw error(peek(), "Expect expression.");
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

    std::vector<std::shared_ptr<Stmt>> Parser::block() {
        std::vector<std::shared_ptr<Stmt>> statements;

        while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
            statements.push_back(declaration());
        }

        consume(TokenType::RIGHT_BRACE, "Expect '}' after block.");
        return statements;
    }

    std::shared_ptr<Stmt> Parser::ifStatement() {
        Token keyword = previous(); // The 'if' token
        consume(TokenType::LEFT_PAREN, "Expect '(' after 'if'.");

        std::shared_ptr<Expr> condition = nullptr;
        std::shared_ptr<VarDeclStmt> declaration = nullptr;

        // --- THE FIX IS HERE ---
        if (match({TokenType::LET})) {
            // This is an `if let` binding, not a regular statement.
            // We must parse it manually and NOT expect a semicolon.
            Token name = consume(TokenType::IDENTIFIER, "Expect variable name after 'let' in 'if' condition.");

            // The type annotation is optional in `if let`.
            std::shared_ptr<ASTType> typeAnnotation = nullptr;
            if (match({TokenType::AS})) {
                typeAnnotation = type();
            }

            consume(TokenType::EQUAL, "Expect '=' to provide an initializer for 'if let'.");
            std::shared_ptr<Expr> initializer = expression();

            // Create the VarDeclStmt. It's implicitly a 'const' binding.
            declaration = std::make_shared<VarDeclStmt>(name, typeAnnotation, initializer, true);
        } else {
            // This is a regular `if` statement with a boolean condition.
            condition = expression();
        }
        // --- END FIX ---

        consume(TokenType::RIGHT_PAREN, "Expect ')' after if condition.");

        // The rest of the logic for parsing the then/else branches is unchanged.
        consume(TokenType::LEFT_BRACE, "Expect '{' before if body.");
        std::shared_ptr<Stmt> thenBranch = std::make_shared<BlockStmt>(block());

        std::shared_ptr<Stmt> elseBranch = nullptr;
        if (match({TokenType::ORIF})) {
            elseBranch = ifStatement();
        } else if (match({TokenType::ELSE})) {
            consume(TokenType::LEFT_BRACE, "Expect '{' before else body.");
            elseBranch = std::make_shared<BlockStmt>(block());
        }

        return std::make_shared<IfStmt>(keyword, condition, thenBranch, elseBranch, declaration);
    }

    std::shared_ptr<Stmt> Parser::whileStatement() {
        Token keyword = previous();

        consume(TokenType::LEFT_PAREN, "Expect '(' after 'while'.");
        std::shared_ptr<Expr> condition = expression();
        consume(TokenType::RIGHT_PAREN, "Expect ')' after while condition.");

        consume(TokenType::LEFT_BRACE, "Expect '{' to begin while loop body.");
        std::shared_ptr<Stmt> body = std::make_shared<BlockStmt>(block());

        return std::make_shared<WhileStmt>(std::move(keyword), std::move(condition), std::move(body));

    }

// Helper method to look ahead for the 'in' keyword
    bool Parser::isForInLoop() {
        // We start looking from the token AFTER the opening '('
        int current = m_current;

        // A for...in loop has the simple structure: IDENTIFIER in EXPRESSION
        // A simple check is to see if an 'in' token appears before a semicolon.
        while (m_tokens[current].type != TokenType::RIGHT_PAREN &&
               m_tokens[current].type != TokenType::EOF_TOKEN) {

            if (m_tokens[current].type == TokenType::IN) {
                return true; // Found 'in', it's a for...in loop
            }
            if (m_tokens[current].type == TokenType::SEMICOLON) {
                return false; // Found ';', it's a C-style loop
            }
            current++;
        }
        return false; // Default to C-style if unsure
    }

// helper for parsing the for...in variant
    std::shared_ptr<Stmt> Parser::parseForInLoop(const Token& keyword) {
        Token name = consume(TokenType::IDENTIFIER, "Expect variable name for for...in loop.");
        consume(TokenType::IN, "Expect 'in' keyword in for...in loop.");
        std::shared_ptr<Expr> collection = expression();
        consume(TokenType::RIGHT_PAREN, "Expect ')' after for..in clauses.");
        consume(TokenType::LEFT_BRACE, "Expect '{' to begin for..in loop body.");
        std::shared_ptr<Stmt> body = std::make_shared<BlockStmt>(block());

        return std::make_shared<ForInStmt>(keyword, std::move(name), std::move(collection), std::move(body));
    }

// helper for parsing the C-style variant
    std::shared_ptr<Stmt> Parser::parseCStyleLoop(const Token& keyword) {
        // --- 1. Initializer Clause ---
        std::shared_ptr<Stmt> initializer;
        if (match({TokenType::SEMICOLON})) {
            initializer = nullptr;
        } else if (match({TokenType::LET})) {
            initializer = varDeclaration(false);
        } else {
            initializer = expressionStatement();
        }

        // --- 2. Condition Clause ---
        std::shared_ptr<Expr> condition = nullptr;
        if (!check(TokenType::SEMICOLON)) {
            condition = expression();
        }
        consume(TokenType::SEMICOLON, "Expect ';' after loop condition.");

        // --- 3. Increment Clause ---
        std::shared_ptr<Expr> increment = nullptr;
        if (!check(TokenType::RIGHT_PAREN)) {
            increment = expression();
        }
        consume(TokenType::RIGHT_PAREN, "Expect ')' after for clauses.");

        // --- 4. Body ---
        consume(TokenType::LEFT_BRACE, "Expect '{' to begin for loop body.");
        std::shared_ptr<Stmt> body = std::make_shared<BlockStmt>(block());

        return std::make_shared<ForStmt>(keyword, std::move(initializer), std::move(condition), std::move(increment), std::move(body));
    }

// Main dispatcher method
    std::shared_ptr<Stmt> Parser::forStatement() {
        Token keyword = previous();

        consume(TokenType::LEFT_PAREN, "Expect '(' after 'for'.");

        if (isForInLoop()) {
            return parseForInLoop(keyword);
        } else {
            return parseCStyleLoop(keyword);
        }
    }

    std::shared_ptr<Expr> Parser::assignment() {
        // 1. Parse the left-hand side, which could be a variable, property, etc.
        std::shared_ptr<Expr> expr = ternary();

        // 2. Check if the next token is ANY assignment operator.
        if (match({TokenType::EQUAL, TokenType::PLUS_EQUAL, TokenType::MINUS_EQUAL,
                   TokenType::STAR_EQUAL, TokenType::SLASH_EQUAL})) {

            Token op = previous();
            std::shared_ptr<Expr> value = assignment(); // Recursively parse the right-hand side.

            // 3. Check if the left-hand side is a valid target.
            if (dynamic_cast<VarExpr *>(expr.get()) ||
                dynamic_cast<GetExpr *>(expr.get()) ||
                dynamic_cast<SubscriptExpr *>(expr.get())) {
                // 4. If it's a valid target, create the AssignExpr, passing the operator token.
                return std::make_shared<AssignExpr>(std::move(expr), op, std::move(value));
            }

            error(op, "Invalid assignment target.");
        }

        // 5. If no assignment operator was found, return the parsed expression.
        return expr;
    }


// Grammar: func IDENTIFIER "(" (IDENTIFIER "as" type ("," IDENTIFIER "as" type)*)? ")" ( "->" type )? "{" block "}"
    std::shared_ptr<Stmt> Parser::function(const std::string& kind) {
        Token name = consume(TokenType::IDENTIFIER, "Expect " + kind + " name.");
        consume(TokenType::LEFT_PAREN, "Expect '(' after " + kind + " name.");

        bool has_this = false;
        std::vector<Parameter> parameters;

        if (!check(TokenType::RIGHT_PAREN)) {
            // The parameter list is not empty.

            // Check if the VERY FIRST parameter is 'this'.
            if (match({TokenType::THIS})) {
                has_this = true;
                // If there's more after 'this', there must be a comma.
                if (!check(TokenType::RIGHT_PAREN)) {
                    consume(TokenType::COMMA, "Expect ',' after 'this' parameter.");
                }
            }

            // Now, parse a comma-separated list of zero-or-more REGULAR parameters.
            // This 'if' condition will be true if:
            //  a) The first token was not 'this'.
            //  b) The first token WAS 'this', and it was followed by a comma.
            if (!check(TokenType::RIGHT_PAREN)) {
                do {
                    Token param_name = consume(TokenType::IDENTIFIER, "Expect parameter name.");
                    consume(TokenType::AS, "Expect 'as' after parameter name.");
                    std::shared_ptr<ASTType> param_type = type();
                    bool is_variadic = match({TokenType::DOT_DOT_DOT});

                    parameters.push_back({param_name, param_type});

                    // Rule: The variadic parameter must be the last one.
                    if (is_variadic && !check(TokenType::RIGHT_PAREN)) {
                        throw error(peek(), "A variadic parameter '...' must be the last parameter in a function signature.");
                    }
                } while (match({TokenType::COMMA}));
            }
        }

        consume(TokenType::RIGHT_PAREN, "Expect ')' after parameters.");

        std::shared_ptr<ASTType> returnType = nullptr;
        if (match({TokenType::MINUS_GREATER})) {
            returnType = type();
        }

        std::optional<std::vector<std::shared_ptr<Stmt>>> body = std::nullopt;
        if (match({TokenType::LEFT_BRACE})) {
            body = block();
        } else if (match({TokenType::SEMICOLON})) {
            // No body
        } else {
            throw error(peek(), "Expect '{' to start a function body or ';' for an interface declaration.");
        }

        return std::make_shared<FuncStmt>(std::move(name), has_this, std::move(parameters), returnType, body);
    }

// Parse a return statement
    std::shared_ptr<Stmt> Parser::returnStatement() {
        Token keyword = previous();
        std::shared_ptr<Expr> value = nullptr;
        if (!check(TokenType::SEMICOLON)) {
            value = expression();
        }
        consume(TokenType::SEMICOLON, "Expect ';' after return value.");
        return std::make_shared<ReturnStmt>(std::move(keyword), std::move(value));
    }

    std::shared_ptr<Stmt> Parser::attachStatement() {
        // Look ahead to see what kind of attach statement this is.
        if (isSelectiveAttach()) {
            // --- Parse the Selective Form: attach name1, name2 from "path" or module_name ---
            std::vector<Token> names;
            do {
                names.push_back(consume(TokenType::IDENTIFIER, "Expect name to attach."));
            } while (match({TokenType::COMMA}));

            consume(TokenType::FROM, "Expect 'from' after attached names.");

            // --- THE FIX IS HERE ---
            Token modulePath;
            if (match({TokenType::STRING})) {
                modulePath = previous();
            } else if (match({TokenType::IDENTIFIER})) {
                modulePath = previous();
            } else {
                throw error(peek(), "Expect module path (string literal) or module name (identifier) after 'from'.");
            }
            // --- END FIX ---

            consume(TokenType::SEMICOLON, "Expect ';' after attach statement.");
            // The 'alias' doesn't make sense in a selective import. We pass std::nullopt.
            return std::make_shared<AttachStmt>(std::move(names), std::move(modulePath), std::nullopt);

        } else {
            // --- Parse the Simple Form: attach "path" or attach name ---
            Token modulePath;
            if (check(TokenType::IDENTIFIER) || check(TokenType::STRING)) {
                modulePath = advance();
            } else {
                throw error(peek(), "Expect module name or path after 'attach'.");
            }

            std::optional<Token> alias;
            if (match({TokenType::AS})) {
                alias = consume(TokenType::IDENTIFIER, "Expect alias name after 'as'.");
            }

            consume(TokenType::SEMICOLON, "Expect ';' after attach statement.");
            return std::make_shared<AttachStmt>(std::vector<Token>{}, modulePath, alias);
        }
    }

// Logical OR (left-associative)
    std::shared_ptr<Expr> Parser::logic_or() {
        std::shared_ptr<Expr> expr = logic_and();
        while (match({TokenType::LOGICAL_OR})) {
            Token op = previous();
            std::shared_ptr<Expr> right = logic_and();
            expr = std::make_shared<LogicalExpr>(std::move(expr), std::move(op), std::move(right));
        }
        return expr;
    }

// Logical AND (left-associative)
    std::shared_ptr<Expr> Parser::logic_and() {
        std::shared_ptr<Expr> expr = equality();
        while (match({TokenType::LOGICAL_AND})) {
            Token op = previous();
            std::shared_ptr<Expr> right = equality();
            expr = std::make_shared<LogicalExpr>(std::move(expr), std::move(op), std::move(right));
        }
        return expr;
    }

    std::shared_ptr<Stmt> Parser::throwStatement() {
        Token keyword = previous();
        std::shared_ptr<Expr> expr = expression();
        consume(TokenType::SEMICOLON, "Expect ';' after throw value.");
        return std::make_shared<ThrowStmt>(std::move(keyword), std::move(expr));
    }

    std::shared_ptr<Stmt> Parser::tryStatement() {
        consume(TokenType::LEFT_BRACE, "Expect '{' after 'try'.");
        std::shared_ptr<Stmt> tryBlock = std::make_shared<BlockStmt>(block());

        consume(TokenType::CATCH, "Expect 'catch' after try block.");
        consume(TokenType::LEFT_PAREN, "Expect '(' after 'catch'.");
        Token catchName = consume(TokenType::IDENTIFIER, "Expect exception variable name.");
        consume(TokenType::RIGHT_PAREN, "Expect ')' after variable name.");

        consume(TokenType::LEFT_BRACE, "Expect '{' after catch clause.");
        std::shared_ptr<Stmt> catchBlock = std::make_shared<BlockStmt>(block());

        return std::make_shared<TryStmt>(std::move(tryBlock), std::move(catchName), std::move(catchBlock));
    }

    std::shared_ptr<Expr> Parser::ternary() {
        std::shared_ptr<Expr> expr = nil_coalescing(); // Its condition can be a '??' expression

        if (match({TokenType::QUESTION})) {
            std::shared_ptr<Expr> thenBranch = expression(); // The middle can be any expression
            consume(TokenType::COLON, "Expect ':' for ternary operator.");
            std::shared_ptr<Expr> elseBranch = ternary(); // The end is another ternary (right-associative)
            expr = std::make_shared<TernaryExpr>(std::move(expr), std::move(thenBranch), std::move(elseBranch));
        }
        return expr;
    }

    std::shared_ptr<Expr> Parser::nil_coalescing() {
        std::shared_ptr<Expr> expr = logic_or(); // Its left-hand side can be a logical OR

        while (match({TokenType::QUESTION_QUESTION})) {
            Token op = previous();
            std::shared_ptr<Expr> right = logic_or(); // Right-hand side is higher precedence
            expr = std::make_shared<LogicalExpr>(std::move(expr), std::move(op), std::move(right));
        }
        return expr;
    }

    bool Parser::isSelectiveAttach() {
        int current = m_current;
        // Look for a 'FROM' keyword before we find a semicolon.
        while (m_tokens[current].type != TokenType::SEMICOLON &&
               m_tokens[current].type != TokenType::EOF_TOKEN) {
            if (m_tokens[current].type == TokenType::FROM) {
                return true; // Found 'from', so it's the selective form.
            }
            current++;
        }
        return false; // No 'from' found, it's the simple form.
    }

    std::shared_ptr<Stmt> Parser::classDeclaration() {
        Token name = consume(TokenType::IDENTIFIER, "Expect class name.");

        // Parse optional inheritance and traits
        std::shared_ptr<VarExpr> superclass = nullptr;
        if (match({TokenType::INHERITS})) {
            consume(TokenType::IDENTIFIER, "Expect superclass name.");
            superclass = std::make_shared<VarExpr>(previous());
        }
        std::vector<std::shared_ptr<VarExpr>> traits;
        if (match({TokenType::USES})) {
            do {
                consume(TokenType::IDENTIFIER, "Expect trait name.");
                traits.push_back(std::make_shared<VarExpr>(previous()));
            } while (match({TokenType::COMMA}));
        }

        // --- NEW: Parse optional contract signing ---
        std::vector<std::shared_ptr<VarExpr>> contracts;
        if (match({TokenType::SIGNS})) {
            do {
                consume(TokenType::IDENTIFIER, "Expect contract name.");
                contracts.push_back(std::make_shared<VarExpr>(previous()));
            } while (match({TokenType::COMMA}));
        }

        consume(TokenType::LEFT_BRACE, "Expect '{' before class body.");

        std::vector<std::shared_ptr<ClassMember>> members;
        AccessLevel current_access = AccessLevel::PRIVATE; // Classes default to private members.

        while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
            // Check for an access specifier first.
            if (match({TokenType::PUBLIC})) {
                consume(TokenType::COLON, "Expect ':' after 'public' specifier.");
                current_access = AccessLevel::PUBLIC;
                continue; // Continue to the next line/member
            }
            if (match({TokenType::PRIVATE})) {
                consume(TokenType::COLON, "Expect ':' after 'private' specifier.");
                current_access = AccessLevel::PRIVATE;
                continue; // Continue to the next line/member
            }

            // If not a specifier, it must be a member declaration.
            bool is_static = match({TokenType::STATIC});

            if (match({TokenType::LET}) || match({TokenType::CONST})) {
                bool is_const = (previous().type == TokenType::CONST);
                // We just consumed 'let' or 'const'. Now call the helper that
                // parses the rest of the declaration.
                auto field_decl = std::static_pointer_cast<VarDeclStmt>(varDeclaration(is_const));
                field_decl->is_static = is_static;
                members.push_back(std::make_shared<FieldMember>(field_decl, current_access));
            }  else if (match({TokenType::FUNC})) {
                auto method_decl = std::static_pointer_cast<FuncStmt>(function("method"));
                method_decl->is_static = is_static;
                members.push_back(std::make_shared<MethodMember>(method_decl, current_access));
            } else {
                throw error(peek(), "Class body can only contain access specifiers ('public:', 'private:') and member declarations ('let', 'func').");
            }
        }

        consume(TokenType::RIGHT_BRACE, "Expect '}' after class body.");

        return std::make_shared<ClassStmt>(std::move(name), std::move(superclass), std::move(contracts), std::move(traits), std::move(members));
    }

    std::shared_ptr<Stmt> Parser::traitDeclaration() {
        Token name = consume(TokenType::IDENTIFIER, "Expect trait name.");
        consume(TokenType::LEFT_BRACE, "Expect '{' before trait body.");

        std::vector<std::shared_ptr<FuncStmt>> methods;
        while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
            // A trait body can ONLY contain 'func' declarations.
            if (match({TokenType::FUNC})) {
                methods.push_back(std::static_pointer_cast<FuncStmt>(function("method")));
            } else {
                // If the user tries to write 'let', it's a syntax error.
                error(peek(), "Trait body can only contain 'func' (method) declarations.");
                if (m_panicMode) break;
                synchronize();
            }
        }

        consume(TokenType::RIGHT_BRACE, "Expect '}' after trait body.");
        return std::make_shared<TraitStmt>(std::move(name), std::move(methods));
    }

std::shared_ptr<Stmt> Parser::contractDeclaration() {
    Token name = consume(TokenType::IDENTIFIER, "Expect contract name.");
    consume(TokenType::LEFT_BRACE, "Expect '{' before contract body.");

    std::vector<std::shared_ptr<ClassMember>> members;

    while (!check(TokenType::RIGHT_BRACE) && !isAtEnd()) {
        if (match({TokenType::PUBLIC})) {
            consume(TokenType::COLON, "Expect ':' after 'public' specifier.");
            continue;
        }
        if (match({TokenType::PRIVATE})) {
            throw error(previous(), "Cannot use 'private' access specifier in a contract. All members are implicitly public.");
        }

        if (match({TokenType::LET}) || match({TokenType::CONST})) {
            bool is_const = (previous().type == TokenType::CONST);

            // --- REVISED LOGIC: MANUALLY parse the field requirement ---
            // We do NOT call varDeclaration() here because the rules are different.

            Token field_name = consume(TokenType::IDENTIFIER, "Expect field name in contract.");

            // RULE: A contract field MUST have an explicit type.
            consume(TokenType::AS, "Expect 'as' to specify a type for a contract field.");
            std::shared_ptr<ASTType> type_ann = type();

            // RULE: A contract field CANNOT have an initializer.
            if (match({TokenType::EQUAL})) {
                throw error(previous(), "A contract field cannot have an initializer. The signing class is responsible for initialization.");
            }

            consume(TokenType::SEMICOLON, "Expect ';' after contract field declaration.");

            // Create the VarDeclStmt with a null initializer. This is now safe
            // because the parser is no longer enforcing the "const must be initialized" rule here.
            auto field_decl = std::make_shared<VarDeclStmt>(field_name, type_ann, nullptr, is_const);
            members.push_back(std::make_shared<FieldMember>(field_decl, AccessLevel::PUBLIC));

        } else if (match({TokenType::FUNC})) {
            auto method_decl = std::static_pointer_cast<FuncStmt>(function("method"));
            if (method_decl->body) {
                throw error(method_decl->name, "A contract method cannot have a body.");
            }
            members.push_back(std::make_shared<MethodMember>(method_decl, AccessLevel::PUBLIC));
        } else {
            throw error(peek(), "Contract body can only contain 'public:', and field ('let', 'const') or method ('func') declarations.");
        }
    }

    consume(TokenType::RIGHT_BRACE, "Expect '}' after contract body.");
    return std::make_shared<ContractStmt>(std::move(name), std::move(members));
}

    std::shared_ptr<Stmt> Parser::breakStatement() {
        Token keyword = previous(); // The 'break' token we just matched
        consume(TokenType::SEMICOLON, "Expect ';' after 'break'.");
        return std::make_shared<BreakStmt>(std::move(keyword));
    }
}

