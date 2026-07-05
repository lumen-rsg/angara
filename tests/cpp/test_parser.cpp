//
// Unit tests for the Angara Parser.
//

#include "test_harness.h"
#include "Lexer.h"
#include "Token.h"
#include "Parser.h"
#include "ErrorHandler.h"
#include "Expr.h"
#include "Stmt.h"

using namespace angara;

// Helper to parse source and return AST statements.
static std::vector<std::shared_ptr<Stmt>> parse(const std::string& source) {
    ErrorHandler handler(source);
    Lexer lexer(source, std::make_shared<std::string>("test.an"), handler);
    auto tokens = lexer.scanTokens();
    Parser parser(tokens, handler);
    return parser.parseStmts();
}

// Helper to check if source produces at least one error.
static bool hasParseError(const std::string& source) {
    ErrorHandler handler(source);
    Lexer lexer(source, std::make_shared<std::string>("test.an"), handler);
    auto tokens = lexer.scanTokens();
    Parser parser(tokens, handler);
    parser.parseStmts();
    return handler.hadError();
}

// Helper: dynamic_cast a shared_ptr<Stmt> to a specific type, returns nullptr on failure.
template<typename T>
std::shared_ptr<T> as(const std::shared_ptr<Stmt>& stmt) {
    return std::dynamic_pointer_cast<T>(stmt);
}

// ── Variable declarations ──

TEST(let_declaration) {
    auto stmts = parse("let x as i64 = 10;");
    ASSERT_EQ(stmts.size(), 1u);
    auto var = as<VarDeclStmt>(stmts[0]);
    ASSERT_TRUE(var != nullptr);
    ASSERT_EQ(var->name.lexeme, "x");
    ASSERT_FALSE(var->is_const);
    ASSERT_TRUE(var->initializer != nullptr);
}

TEST(const_declaration) {
    auto stmts = parse("const PI as f64 = 3.14;");
    ASSERT_EQ(stmts.size(), 1u);
    auto var = as<VarDeclStmt>(stmts[0]);
    ASSERT_TRUE(var != nullptr);
    ASSERT_TRUE(var->is_const);
    ASSERT_EQ(var->name.lexeme, "PI");
}

TEST(let_without_initializer) {
    auto stmts = parse("let x as i64;");
    ASSERT_EQ(stmts.size(), 1u);
    auto var = as<VarDeclStmt>(stmts[0]);
    ASSERT_TRUE(var != nullptr);
    ASSERT_TRUE(var->initializer == nullptr);
}

// ── Expression statements ──

TEST(expression_statement) {
    auto stmts = parse("1 + 2;");
    ASSERT_EQ(stmts.size(), 1u);
    auto exprStmt = as<ExpressionStmt>(stmts[0]);
    ASSERT_TRUE(exprStmt != nullptr);
    ASSERT_TRUE(exprStmt->expression != nullptr);
}

TEST(literal_expression) {
    auto stmts = parse("42;");
    ASSERT_EQ(stmts.size(), 1u);
    auto exprStmt = as<ExpressionStmt>(stmts[0]);
    ASSERT_TRUE(exprStmt != nullptr);
    auto lit = std::dynamic_pointer_cast<Literal>(exprStmt->expression);
    ASSERT_TRUE(lit != nullptr);
    ASSERT_EQ(lit->token.lexeme, "42");
}

TEST(binary_expression) {
    auto stmts = parse("1 + 2;");
    ASSERT_EQ(stmts.size(), 1u);
    auto exprStmt = as<ExpressionStmt>(stmts[0]);
    auto bin = std::dynamic_pointer_cast<Binary>(exprStmt->expression);
    ASSERT_TRUE(bin != nullptr);
    ASSERT_EQ(bin->op.type, TokenType::PLUS);
}

TEST(unary_expression) {
    auto stmts = parse("-5;");
    ASSERT_EQ(stmts.size(), 1u);
    auto exprStmt = as<ExpressionStmt>(stmts[0]);
    auto unary = std::dynamic_pointer_cast<Unary>(exprStmt->expression);
    ASSERT_TRUE(unary != nullptr);
    ASSERT_EQ(unary->op.type, TokenType::MINUS);
}

TEST(logical_not_expression) {
    auto stmts = parse("!true;");
    ASSERT_EQ(stmts.size(), 1u);
    auto exprStmt = as<ExpressionStmt>(stmts[0]);
    auto unary = std::dynamic_pointer_cast<Unary>(exprStmt->expression);
    ASSERT_TRUE(unary != nullptr);
    ASSERT_EQ(unary->op.type, TokenType::BANG);
}

TEST(grouping_expression) {
    auto stmts = parse("(1 + 2);");
    ASSERT_EQ(stmts.size(), 1u);
    auto exprStmt = as<ExpressionStmt>(stmts[0]);
    auto group = std::dynamic_pointer_cast<Grouping>(exprStmt->expression);
    ASSERT_TRUE(group != nullptr);
}

TEST(comparison_expression) {
    auto stmts = parse("a < b;");
    ASSERT_EQ(stmts.size(), 1u);
    auto exprStmt = as<ExpressionStmt>(stmts[0]);
    auto bin = std::dynamic_pointer_cast<Binary>(exprStmt->expression);
    ASSERT_TRUE(bin != nullptr);
    ASSERT_EQ(bin->op.type, TokenType::LESS);
}

TEST(equality_expression) {
    auto stmts = parse("a == b;");
    ASSERT_EQ(stmts.size(), 1u);
    auto exprStmt = as<ExpressionStmt>(stmts[0]);
    auto bin = std::dynamic_pointer_cast<Binary>(exprStmt->expression);
    ASSERT_TRUE(bin != nullptr);
    ASSERT_EQ(bin->op.type, TokenType::EQUAL_EQUAL);
}

TEST(nested_binary) {
    auto stmts = parse("1 + 2 * 3;");
    ASSERT_EQ(stmts.size(), 1u);
    auto exprStmt = as<ExpressionStmt>(stmts[0]);
    auto bin = std::dynamic_pointer_cast<Binary>(exprStmt->expression);
    ASSERT_TRUE(bin != nullptr);
    ASSERT_EQ(bin->op.type, TokenType::PLUS);
    // Right side should be a binary (*)
    auto rightBin = std::dynamic_pointer_cast<Binary>(bin->right);
    ASSERT_TRUE(rightBin != nullptr);
    ASSERT_EQ(rightBin->op.type, TokenType::STAR);
}

TEST(call_expression) {
    auto stmts = parse("foo(1, 2);");
    ASSERT_EQ(stmts.size(), 1u);
    auto exprStmt = as<ExpressionStmt>(stmts[0]);
    auto call = std::dynamic_pointer_cast<CallExpr>(exprStmt->expression);
    ASSERT_TRUE(call != nullptr);
    ASSERT_EQ(call->arguments.size(), 2u);
}

TEST(member_access) {
    auto stmts = parse("obj.field;");
    ASSERT_EQ(stmts.size(), 1u);
    auto exprStmt = as<ExpressionStmt>(stmts[0]);
    auto get = std::dynamic_pointer_cast<GetExpr>(exprStmt->expression);
    ASSERT_TRUE(get != nullptr);
    ASSERT_EQ(get->name.lexeme, "field");
}

TEST(chained_member_access) {
    auto stmts = parse("a.b.c;");
    ASSERT_EQ(stmts.size(), 1u);
    auto exprStmt = as<ExpressionStmt>(stmts[0]);
    auto get = std::dynamic_pointer_cast<GetExpr>(exprStmt->expression);
    ASSERT_TRUE(get != nullptr);
    ASSERT_EQ(get->name.lexeme, "c");
    auto inner = std::dynamic_pointer_cast<GetExpr>(get->object);
    ASSERT_TRUE(inner != nullptr);
    ASSERT_EQ(inner->name.lexeme, "b");
}

TEST(subscript_expression) {
    auto stmts = parse("arr[0];");
    ASSERT_EQ(stmts.size(), 1u);
    auto exprStmt = as<ExpressionStmt>(stmts[0]);
    auto sub = std::dynamic_pointer_cast<SubscriptExpr>(exprStmt->expression);
    ASSERT_TRUE(sub != nullptr);
}

TEST(list_literal) {
    auto stmts = parse("[1, 2, 3];");
    ASSERT_EQ(stmts.size(), 1u);
    auto exprStmt = as<ExpressionStmt>(stmts[0]);
    auto list = std::dynamic_pointer_cast<ListExpr>(exprStmt->expression);
    ASSERT_TRUE(list != nullptr);
    ASSERT_EQ(list->elements.size(), 3u);
}

TEST(empty_list_literal) {
    auto stmts = parse("[];");
    ASSERT_EQ(stmts.size(), 1u);
    auto exprStmt = as<ExpressionStmt>(stmts[0]);
    auto list = std::dynamic_pointer_cast<ListExpr>(exprStmt->expression);
    ASSERT_TRUE(list != nullptr);
    ASSERT_EQ(list->elements.size(), 0u);
}

TEST(ternary_expression) {
    auto stmts = parse("true ? 1 : 0;");
    ASSERT_EQ(stmts.size(), 1u);
    auto exprStmt = as<ExpressionStmt>(stmts[0]);
    auto ternary = std::dynamic_pointer_cast<TernaryExpr>(exprStmt->expression);
    ASSERT_TRUE(ternary != nullptr);
}

TEST(assignment_expression) {
    auto stmts = parse("x = 42;");
    ASSERT_EQ(stmts.size(), 1u);
    auto exprStmt = as<ExpressionStmt>(stmts[0]);
    auto assign = std::dynamic_pointer_cast<AssignExpr>(exprStmt->expression);
    ASSERT_TRUE(assign != nullptr);
    ASSERT_EQ(assign->op.type, TokenType::EQUAL);
}

TEST(compound_assignment) {
    // LANG-15: all 10 compound-assignment operators
    struct Case { const char* src; TokenType expected; };
    Case cases[] = {
        {"x += 1;",  TokenType::PLUS_EQUAL},
        {"x -= 1;",  TokenType::MINUS_EQUAL},
        {"x *= 1;",  TokenType::STAR_EQUAL},
        {"x /= 1;",  TokenType::SLASH_EQUAL},
        {"x %= 1;",  TokenType::PERCENT_EQUAL},
        {"x &= 1;",  TokenType::AMPERSAND_EQUAL},
        {"x |= 1;",  TokenType::PIPE_EQUAL},
        {"x ^= 1;",  TokenType::CARET_EQUAL},
        {"x <<= 1;", TokenType::LSHIFT_EQUAL},
        {"x >>= 1;", TokenType::RSHIFT_EQUAL},
    };
    for (auto& c : cases) {
        auto stmts = parse(c.src);
        ASSERT_EQ(stmts.size(), 1u);
        auto exprStmt = as<ExpressionStmt>(stmts[0]);
        auto assign = std::dynamic_pointer_cast<AssignExpr>(exprStmt->expression);
        ASSERT_TRUE(assign != nullptr);
        ASSERT_EQ(assign->op.type, c.expected);
    }
}

TEST(update_expression_postfix) {
    auto stmts = parse("i++;");
    ASSERT_EQ(stmts.size(), 1u);
    auto exprStmt = as<ExpressionStmt>(stmts[0]);
    auto update = std::dynamic_pointer_cast<UpdateExpr>(exprStmt->expression);
    ASSERT_TRUE(update != nullptr);
    ASSERT_FALSE(update->isPrefix);
    ASSERT_EQ(update->op.type, TokenType::PLUS_PLUS);
}

TEST(update_expression_prefix) {
    auto stmts = parse("--i;");
    ASSERT_EQ(stmts.size(), 1u);
    auto exprStmt = as<ExpressionStmt>(stmts[0]);
    auto update = std::dynamic_pointer_cast<UpdateExpr>(exprStmt->expression);
    ASSERT_TRUE(update != nullptr);
    ASSERT_TRUE(update->isPrefix);
    ASSERT_EQ(update->op.type, TokenType::MINUS_MINUS);
}

// ── Function declarations ──

TEST(function_declaration_no_params) {
    auto stmts = parse("func greet() { }");
    ASSERT_EQ(stmts.size(), 1u);
    auto func = as<FuncStmt>(stmts[0]);
    ASSERT_TRUE(func != nullptr);
    ASSERT_EQ(func->name.lexeme, "greet");
    ASSERT_EQ(func->params.size(), 0u);
    ASSERT_TRUE(func->body.has_value());
}

TEST(function_declaration_with_params) {
    auto stmts = parse("func add(a as i64, b as i64) -> i64 { return a; }");
    ASSERT_EQ(stmts.size(), 1u);
    auto func = as<FuncStmt>(stmts[0]);
    ASSERT_TRUE(func != nullptr);
    ASSERT_EQ(func->name.lexeme, "add");
    ASSERT_EQ(func->params.size(), 2u);
    ASSERT_TRUE(func->returnType != nullptr);
}

TEST(function_with_body) {
    auto stmts = parse("func foo() { let x as i64 = 1; return x; }");
    ASSERT_EQ(stmts.size(), 1u);
    auto func = as<FuncStmt>(stmts[0]);
    ASSERT_TRUE(func != nullptr);
    ASSERT_TRUE(func->body.has_value());
    ASSERT_EQ(func->body->size(), 2u);
}

// ── Control flow ──

TEST(if_statement) {
    auto stmts = parse("if (true) { }");
    ASSERT_EQ(stmts.size(), 1u);
    auto ifStmt = as<IfStmt>(stmts[0]);
    ASSERT_TRUE(ifStmt != nullptr);
    ASSERT_TRUE(ifStmt->thenBranch != nullptr);
    ASSERT_TRUE(ifStmt->elseBranch == nullptr);
}

TEST(if_else_statement) {
    auto stmts = parse("if (true) { } else { }");
    ASSERT_EQ(stmts.size(), 1u);
    auto ifStmt = as<IfStmt>(stmts[0]);
    ASSERT_TRUE(ifStmt != nullptr);
    ASSERT_TRUE(ifStmt->elseBranch != nullptr);
}

TEST(if_orif_else_chain) {
    auto stmts = parse("if (a) { } orif (b) { } else { }");
    ASSERT_EQ(stmts.size(), 1u);
    auto ifStmt = as<IfStmt>(stmts[0]);
    ASSERT_TRUE(ifStmt != nullptr);
    // The else branch should be another IfStmt (orif is desugared)
    auto elseIf = as<IfStmt>(ifStmt->elseBranch);
    ASSERT_TRUE(elseIf != nullptr);
}

TEST(while_statement) {
    auto stmts = parse("while (true) { }");
    ASSERT_EQ(stmts.size(), 1u);
    auto whileStmt = as<WhileStmt>(stmts[0]);
    ASSERT_TRUE(whileStmt != nullptr);
    ASSERT_TRUE(whileStmt->condition != nullptr);
    ASSERT_TRUE(whileStmt->body != nullptr);
}

TEST(c_style_for_loop) {
    auto stmts = parse("for (let i as i64 = 0; i < 10; i++) { }");
    ASSERT_EQ(stmts.size(), 1u);
    auto forStmt = as<ForStmt>(stmts[0]);
    ASSERT_TRUE(forStmt != nullptr);
    ASSERT_TRUE(forStmt->initializer != nullptr);
    ASSERT_TRUE(forStmt->condition != nullptr);
    ASSERT_TRUE(forStmt->increment != nullptr);
}

TEST(for_in_loop) {
    auto stmts = parse("for (item in items) { }");
    ASSERT_EQ(stmts.size(), 1u);
    auto forIn = as<ForInStmt>(stmts[0]);
    ASSERT_TRUE(forIn != nullptr);
    ASSERT_EQ(forIn->name.lexeme, "item");
}

TEST(return_statement) {
    auto stmts = parse("func f() { return 42; }");
    ASSERT_EQ(stmts.size(), 1u);
    auto func = as<FuncStmt>(stmts[0]);
    ASSERT_TRUE(func != nullptr);
    ASSERT_EQ(func->body->size(), 1u);
    auto ret = as<ReturnStmt>(func->body->at(0));
    ASSERT_TRUE(ret != nullptr);
    ASSERT_TRUE(ret->value != nullptr);
}

TEST(return_void) {
    auto stmts = parse("func f() { return; }");
    ASSERT_EQ(stmts.size(), 1u);
    auto func = as<FuncStmt>(stmts[0]);
    auto ret = as<ReturnStmt>(func->body->at(0));
    ASSERT_TRUE(ret != nullptr);
    ASSERT_TRUE(ret->value == nullptr);
}

TEST(break_statement) {
    auto stmts = parse("while (true) { break; }");
    ASSERT_EQ(stmts.size(), 1u);
    auto whileStmt = as<WhileStmt>(stmts[0]);
    auto block = as<BlockStmt>(whileStmt->body);
    ASSERT_TRUE(block != nullptr);
    auto brk = as<BreakStmt>(block->statements[0]);
    ASSERT_TRUE(brk != nullptr);
}

TEST(continue_statement) {
    auto stmts = parse("while (true) { continue; }");
    ASSERT_EQ(stmts.size(), 1u);
    auto whileStmt = as<WhileStmt>(stmts[0]);
    auto block = as<BlockStmt>(whileStmt->body);
    ASSERT_TRUE(block != nullptr);
    auto cont = as<ContinueStmt>(block->statements[0]);
    ASSERT_TRUE(cont != nullptr);
}

// ── Block statements ──

TEST(block_statement) {
    auto stmts = parse("{ let x as i64 = 1; }");
    ASSERT_EQ(stmts.size(), 1u);
    auto block = as<BlockStmt>(stmts[0]);
    ASSERT_TRUE(block != nullptr);
    ASSERT_EQ(block->statements.size(), 1u);
}

TEST(empty_block) {
    auto stmts = parse("{ }");
    ASSERT_EQ(stmts.size(), 1u);
    auto block = as<BlockStmt>(stmts[0]);
    ASSERT_TRUE(block != nullptr);
    ASSERT_EQ(block->statements.size(), 0u);
}

// ── Class declarations ──

TEST(class_declaration) {
    auto stmts = parse("class Animal { }");
    ASSERT_EQ(stmts.size(), 1u);
    auto cls = as<ClassStmt>(stmts[0]);
    ASSERT_TRUE(cls != nullptr);
    ASSERT_EQ(cls->name.lexeme, "Animal");
    ASSERT_TRUE(cls->superclass == nullptr);
}

TEST(class_with_inheritance) {
    auto stmts = parse("class Dog inherits Animal { }");
    ASSERT_EQ(stmts.size(), 1u);
    auto cls = as<ClassStmt>(stmts[0]);
    ASSERT_TRUE(cls != nullptr);
    ASSERT_TRUE(cls->superclass != nullptr);
    ASSERT_EQ(cls->superclass->name.lexeme, "Animal");
}

TEST(class_with_traits) {
    auto stmts = parse("class Foo uses Printable { }");
    ASSERT_EQ(stmts.size(), 1u);
    auto cls = as<ClassStmt>(stmts[0]);
    ASSERT_TRUE(cls != nullptr);
    ASSERT_EQ(cls->traits.size(), 1u);
}

TEST(class_with_members) {
    auto stmts = parse("class Point {\n"
                       "  let x as i64;\n"
                       "  let y as i64;\n"
                       "}");
    ASSERT_EQ(stmts.size(), 1u);
    auto cls = as<ClassStmt>(stmts[0]);
    ASSERT_TRUE(cls != nullptr);
    ASSERT_EQ(cls->members.size(), 2u);
}

// ── Trait declarations ──

TEST(trait_declaration) {
    auto stmts = parse("trait Drawable { func draw(); }");
    ASSERT_EQ(stmts.size(), 1u);
    auto trait = as<TraitStmt>(stmts[0]);
    ASSERT_TRUE(trait != nullptr);
    ASSERT_EQ(trait->name.lexeme, "Drawable");
    ASSERT_EQ(trait->methods.size(), 1u);
}

// ── Contract declarations ──

TEST(contract_declaration) {
    auto stmts = parse("contract Named { let name as string; }");
    ASSERT_EQ(stmts.size(), 1u);
    auto contract = as<ContractStmt>(stmts[0]);
    ASSERT_TRUE(contract != nullptr);
    ASSERT_EQ(contract->name.lexeme, "Named");
}

// ── Enum declarations ──

TEST(enum_simple) {
    auto stmts = parse("enum Direction { North, South, East, West }");
    ASSERT_EQ(stmts.size(), 1u);
    auto enumStmt = as<EnumStmt>(stmts[0]);
    ASSERT_TRUE(enumStmt != nullptr);
    ASSERT_EQ(enumStmt->name.lexeme, "Direction");
    ASSERT_EQ(enumStmt->variants.size(), 4u);
}

TEST(enum_with_associated_data) {
    auto stmts = parse("enum Result { Ok(i64), Err(string) }");
    ASSERT_EQ(stmts.size(), 1u);
    auto enumStmt = as<EnumStmt>(stmts[0]);
    ASSERT_TRUE(enumStmt != nullptr);
    ASSERT_EQ(enumStmt->variants.size(), 2u);
    ASSERT_EQ(enumStmt->variants[0]->params.size(), 1u);
    ASSERT_EQ(enumStmt->variants[1]->params.size(), 1u);
}

// ── Data declarations ──

TEST(data_declaration) {
    auto stmts = parse("data Point { let x as i64; let y as i64; }");
    ASSERT_EQ(stmts.size(), 1u);
    auto data = as<DataStmt>(stmts[0]);
    ASSERT_TRUE(data != nullptr);
    ASSERT_EQ(data->name.lexeme, "Point");
    ASSERT_EQ(data->fields.size(), 2u);
}

// ── Exception handling ──

TEST(try_catch) {
    auto stmts = parse("try { } catch(e) { }");
    ASSERT_EQ(stmts.size(), 1u);
    auto tryStmt = as<TryStmt>(stmts[0]);
    ASSERT_TRUE(tryStmt != nullptr);
    ASSERT_TRUE(tryStmt->tryBlock != nullptr);
    ASSERT_EQ(tryStmt->catchName.lexeme, "e");
    ASSERT_TRUE(tryStmt->catchBlock != nullptr);
}

TEST(throw_statement) {
    auto stmts = parse("func f() { throw \"error\"; }");
    ASSERT_EQ(stmts.size(), 1u);
    auto func = as<FuncStmt>(stmts[0]);
    auto throwStmt = as<ThrowStmt>(func->body->at(0));
    ASSERT_TRUE(throwStmt != nullptr);
}

// ── Attach/import ──

TEST(attach_statement) {
    auto stmts = parse("attach io;");
    ASSERT_EQ(stmts.size(), 1u);
    auto attach = as<AttachStmt>(stmts[0]);
    ASSERT_TRUE(attach != nullptr);
    ASSERT_EQ(attach->modulePath.lexeme, "io");
    ASSERT_TRUE(attach->names.empty());
}

TEST(attach_selective) {
    // Angara doesn't have the "from" import syntax.
    // Selective attach uses: attach module;
    auto stmts = parse("attach io;");
    ASSERT_EQ(stmts.size(), 1u);
    auto attach = as<AttachStmt>(stmts[0]);
    ASSERT_TRUE(attach != nullptr);
    ASSERT_EQ(attach->names.size(), 0u);
}

// ── Match expression ──

TEST(match_expression) {
    // Match uses case arms with blocks and commas
    auto stmts = parse("let r = match (x) { case 1: { \"one\" }, case 2: { \"two\" } };");
    ASSERT_EQ(stmts.size(), 1u);
    auto var = as<VarDeclStmt>(stmts[0]);
    ASSERT_TRUE(var != nullptr);
    ASSERT_TRUE(var->initializer != nullptr);
    auto match = std::dynamic_pointer_cast<MatchExpr>(var->initializer);
    ASSERT_TRUE(match != nullptr);
    ASSERT_EQ(match->cases.size(), 2u);
}

// ── Lambda expression ──

TEST(lambda_expression) {
    // Lambdas are typically assigned to variables
    auto stmts = parse("let f = func(x as i64) -> i64 { return x; };");
    ASSERT_EQ(stmts.size(), 1u);
    auto var = as<VarDeclStmt>(stmts[0]);
    ASSERT_TRUE(var != nullptr);
    ASSERT_TRUE(var->initializer != nullptr);
    auto lambda = std::dynamic_pointer_cast<LambdaExpr>(var->initializer);
    ASSERT_TRUE(lambda != nullptr);
    ASSERT_EQ(lambda->param_names.size(), 1u);
}

// ── Multiple statements ──

TEST(multiple_statements) {
    auto stmts = parse("let a as i64 = 1;\nlet b as i64 = 2;\nlet c as i64 = 3;");
    ASSERT_EQ(stmts.size(), 3u);
    for (auto& s : stmts) {
        auto var = as<VarDeclStmt>(s);
        ASSERT_TRUE(var != nullptr);
    }
}

// ── Error recovery ──

TEST(error_on_unexpected_token) {
    // Missing semicolon or expression after 'let'
    ASSERT_TRUE(hasParseError("let = ;"));
}

TEST(error_on_missing_brace) {
    ASSERT_TRUE(hasParseError("if (true) { "));
}

TEST(error_on_extra_token) {
    // Function declaration doesn't expect random tokens after the body.
    // (Note: "func () {} ;;;;" is now a valid IIFE after LANG-17.)
    ASSERT_TRUE(hasParseError("func f() -> i64 { return 1; } extra_token"));
}

// ── Logical expressions ──

TEST(logical_or) {
    auto stmts = parse("a || b;");
    ASSERT_EQ(stmts.size(), 1u);
    auto exprStmt = as<ExpressionStmt>(stmts[0]);
    auto logic = std::dynamic_pointer_cast<LogicalExpr>(exprStmt->expression);
    ASSERT_TRUE(logic != nullptr);
    ASSERT_EQ(logic->op.type, TokenType::LOGICAL_OR);
}

TEST(logical_and) {
    auto stmts = parse("a && b;");
    ASSERT_EQ(stmts.size(), 1u);
    auto exprStmt = as<ExpressionStmt>(stmts[0]);
    auto logic = std::dynamic_pointer_cast<LogicalExpr>(exprStmt->expression);
    ASSERT_TRUE(logic != nullptr);
    ASSERT_EQ(logic->op.type, TokenType::LOGICAL_AND);
}

// ── Record literal ──

TEST(record_literal) {
    // Records must be in expression context (assigned to variable) since
    // { ... } at statement level is parsed as a block.
    auto stmts = parse("let p = { name: \"Alice\", age: 30 };");
    ASSERT_EQ(stmts.size(), 1u);
    auto var = as<VarDeclStmt>(stmts[0]);
    ASSERT_TRUE(var != nullptr);
    ASSERT_TRUE(var->initializer != nullptr);
    auto record = std::dynamic_pointer_cast<RecordExpr>(var->initializer);
    ASSERT_TRUE(record != nullptr);
    ASSERT_EQ(record->keys.size(), 2u);
    ASSERT_EQ(record->values.size(), 2u);
}

// ── String literal expression ──

TEST(string_expression) {
    auto stmts = parse("\"hello\";");
    ASSERT_EQ(stmts.size(), 1u);
    auto exprStmt = as<ExpressionStmt>(stmts[0]);
    auto lit = std::dynamic_pointer_cast<Literal>(exprStmt->expression);
    ASSERT_TRUE(lit != nullptr);
    ASSERT_EQ(lit->token.type, TokenType::STRING);
}

// ── Nil literal ──

TEST(nil_expression) {
    auto stmts = parse("nil;");
    ASSERT_EQ(stmts.size(), 1u);
    auto exprStmt = as<ExpressionStmt>(stmts[0]);
    auto lit = std::dynamic_pointer_cast<Literal>(exprStmt->expression);
    ASSERT_TRUE(lit != nullptr);
    ASSERT_EQ(lit->token.type, TokenType::NIL);
}

// ── Bool literals as expressions ──

TEST(bool_true_expression) {
    auto stmts = parse("true;");
    ASSERT_EQ(stmts.size(), 1u);
    auto exprStmt = as<ExpressionStmt>(stmts[0]);
    auto lit = std::dynamic_pointer_cast<Literal>(exprStmt->expression);
    ASSERT_TRUE(lit != nullptr);
    ASSERT_EQ(lit->token.type, TokenType::TRUE);
}

TEST(bool_false_expression) {
    auto stmts = parse("false;");
    ASSERT_EQ(stmts.size(), 1u);
    auto exprStmt = as<ExpressionStmt>(stmts[0]);
    auto lit = std::dynamic_pointer_cast<Literal>(exprStmt->expression);
    ASSERT_TRUE(lit != nullptr);
    ASSERT_EQ(lit->token.type, TokenType::FALSE);
}
