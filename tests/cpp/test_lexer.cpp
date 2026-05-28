//
// Unit tests for the Angara Lexer.
//

#include "test_harness.h"
#include "Lexer.h"
#include "Token.h"
#include "ErrorHandler.h"

using namespace angara;

// Helper to scan source and return tokens.
static std::vector<Token> scan(const std::string& source) {
    ErrorHandler handler(source);
    Lexer lexer(source, std::make_shared<std::string>("test.an"), handler);
    return lexer.scanTokens();
}

// ── Single-character tokens ──

TEST(single_char_operators) {
    auto tokens = scan("+ - * / % , . ; : ? @");
    // + - * / % , . ; : ? @ = 11 operators + EOF
    ASSERT_EQ(tokens.size(), 12u);
    ASSERT_EQ(tokens[0].type, TokenType::PLUS);
    ASSERT_EQ(tokens[1].type, TokenType::MINUS);
    ASSERT_EQ(tokens[2].type, TokenType::STAR);
    ASSERT_EQ(tokens[3].type, TokenType::SLASH);
    ASSERT_EQ(tokens[4].type, TokenType::PERCENT);
    ASSERT_EQ(tokens[5].type, TokenType::COMMA);
    ASSERT_EQ(tokens[6].type, TokenType::DOT);
    ASSERT_EQ(tokens[7].type, TokenType::SEMICOLON);
    ASSERT_EQ(tokens[8].type, TokenType::COLON);
    ASSERT_EQ(tokens[9].type, TokenType::QUESTION);
    ASSERT_EQ(tokens[10].type, TokenType::AT_SIGN);
    ASSERT_EQ(tokens[11].type, TokenType::EOF_TOKEN);
}

TEST(brackets) {
    auto tokens = scan("( ) { } [ ]");
    ASSERT_EQ(tokens.size(), 7u);
    ASSERT_EQ(tokens[0].type, TokenType::LEFT_PAREN);
    ASSERT_EQ(tokens[1].type, TokenType::RIGHT_PAREN);
    ASSERT_EQ(tokens[2].type, TokenType::LEFT_BRACE);
    ASSERT_EQ(tokens[3].type, TokenType::RIGHT_BRACE);
    ASSERT_EQ(tokens[4].type, TokenType::LEFT_BRACKET);
    ASSERT_EQ(tokens[5].type, TokenType::RIGHT_BRACKET);
}

// ── Two-character operators ──

TEST(two_char_operators) {
    auto tokens = scan("++ -- && || += -= *= /=");
    ASSERT_EQ(tokens.size(), 9u); // 8 operators + EOF
    ASSERT_EQ(tokens[0].type, TokenType::PLUS_PLUS);
    ASSERT_EQ(tokens[1].type, TokenType::MINUS_MINUS);
    ASSERT_EQ(tokens[2].type, TokenType::LOGICAL_AND);
    ASSERT_EQ(tokens[3].type, TokenType::LOGICAL_OR);
    ASSERT_EQ(tokens[4].type, TokenType::PLUS_EQUAL);
    ASSERT_EQ(tokens[5].type, TokenType::MINUS_EQUAL);
    ASSERT_EQ(tokens[6].type, TokenType::STAR_EQUAL);
    ASSERT_EQ(tokens[7].type, TokenType::SLASH_EQUAL);
}

TEST(comparison_operators) {
    auto tokens = scan("! != = == > >= < <=");
    ASSERT_EQ(tokens.size(), 9u);
    ASSERT_EQ(tokens[0].type, TokenType::BANG);
    ASSERT_EQ(tokens[1].type, TokenType::BANG_EQUAL);
    ASSERT_EQ(tokens[2].type, TokenType::EQUAL);
    ASSERT_EQ(tokens[3].type, TokenType::EQUAL_EQUAL);
    ASSERT_EQ(tokens[4].type, TokenType::GREATER);
    ASSERT_EQ(tokens[5].type, TokenType::GREATER_EQUAL);
    ASSERT_EQ(tokens[6].type, TokenType::LESS);
    ASSERT_EQ(tokens[7].type, TokenType::LESS_EQUAL);
}

TEST(arrow_operator) {
    auto tokens = scan("->");
    ASSERT_EQ(tokens.size(), 2u);
    ASSERT_EQ(tokens[0].type, TokenType::MINUS_GREATER);
}

// ── Literals ──

TEST(integer_literal) {
    auto tokens = scan("42");
    ASSERT_EQ(tokens.size(), 2u);
    ASSERT_EQ(tokens[0].type, TokenType::NUMBER_INT);
    ASSERT_EQ(tokens[0].lexeme, "42");
}

TEST(float_literal) {
    auto tokens = scan("3.14");
    ASSERT_EQ(tokens.size(), 2u);
    ASSERT_EQ(tokens[0].type, TokenType::NUMBER_FLOAT);
    ASSERT_EQ(tokens[0].lexeme, "3.14");
}

TEST(hex_literal) {
    auto tokens = scan("0xFF");
    ASSERT_EQ(tokens.size(), 2u);
    ASSERT_EQ(tokens[0].type, TokenType::NUMBER_INT);
    ASSERT_EQ(tokens[0].lexeme, "0xFF");
}

TEST(binary_literal) {
    auto tokens = scan("0b1010");
    ASSERT_EQ(tokens.size(), 2u);
    ASSERT_EQ(tokens[0].type, TokenType::NUMBER_INT);
    ASSERT_EQ(tokens[0].lexeme, "0b1010");
}

TEST(integer_with_underscores) {
    auto tokens = scan("1_000_000");
    ASSERT_EQ(tokens.size(), 2u);
    ASSERT_EQ(tokens[0].type, TokenType::NUMBER_INT);
    ASSERT_EQ(tokens[0].lexeme, "1_000_000");
}

TEST(string_literal) {
    auto tokens = scan("\"hello world\"");
    ASSERT_EQ(tokens.size(), 2u);
    ASSERT_EQ(tokens[0].type, TokenType::STRING);
}

TEST(string_with_escapes) {
    auto tokens = scan("\"line1\\nline2\\ttab\"");
    ASSERT_EQ(tokens.size(), 2u);
    ASSERT_EQ(tokens[0].type, TokenType::STRING);
}

TEST(triple_quoted_string) {
    auto tokens = scan("\"\"\"hello\nworld\"\"\"");
    ASSERT_EQ(tokens.size(), 2u);
    ASSERT_EQ(tokens[0].type, TokenType::STRING);
}

// ── Identifiers and keywords ──

TEST(identifiers) {
    auto tokens = scan("foo bar_baz x123");
    ASSERT_EQ(tokens.size(), 4u);
    ASSERT_EQ(tokens[0].type, TokenType::IDENTIFIER);
    ASSERT_EQ(tokens[0].lexeme, "foo");
    ASSERT_EQ(tokens[1].type, TokenType::IDENTIFIER);
    ASSERT_EQ(tokens[1].lexeme, "bar_baz");
    ASSERT_EQ(tokens[2].type, TokenType::IDENTIFIER);
    ASSERT_EQ(tokens[2].lexeme, "x123");
}

TEST(keywords) {
    auto tokens = scan("let func return if else for while");
    ASSERT_EQ(tokens.size(), 8u);
    ASSERT_EQ(tokens[0].type, TokenType::LET);
    ASSERT_EQ(tokens[1].type, TokenType::FUNC);
    ASSERT_EQ(tokens[2].type, TokenType::RETURN);
    ASSERT_EQ(tokens[3].type, TokenType::IF);
    ASSERT_EQ(tokens[4].type, TokenType::ELSE);
    ASSERT_EQ(tokens[5].type, TokenType::FOR);
    ASSERT_EQ(tokens[6].type, TokenType::WHILE);
}

TEST(type_names_are_identifiers) {
    // Type names like i64, f64, string, bool are not lexer keywords —
    // they're resolved as types at the parser/type-checker level.
    auto tokens = scan("i64 f64 string bool");
    ASSERT_EQ(tokens.size(), 5u);
    ASSERT_EQ(tokens[0].type, TokenType::IDENTIFIER);
    ASSERT_EQ(tokens[0].lexeme, "i64");
    ASSERT_EQ(tokens[1].type, TokenType::IDENTIFIER);
    ASSERT_EQ(tokens[1].lexeme, "f64");
    ASSERT_EQ(tokens[2].type, TokenType::IDENTIFIER);
    ASSERT_EQ(tokens[2].lexeme, "string");
    ASSERT_EQ(tokens[3].type, TokenType::IDENTIFIER);
    ASSERT_EQ(tokens[3].lexeme, "bool");
}

// ── Comments ──

TEST(line_comment) {
    auto tokens = scan("x // this is a comment\ny");
    ASSERT_EQ(tokens.size(), 3u); // x, y, EOF
    ASSERT_EQ(tokens[0].lexeme, "x");
    ASSERT_EQ(tokens[1].lexeme, "y");
}

TEST(block_comment) {
    auto tokens = scan("x /* block comment */ y");
    ASSERT_EQ(tokens.size(), 3u);
    ASSERT_EQ(tokens[0].lexeme, "x");
    ASSERT_EQ(tokens[1].lexeme, "y");
}

// ── Edge cases ──

TEST(empty_source) {
    auto tokens = scan("");
    ASSERT_EQ(tokens.size(), 1u);
    ASSERT_EQ(tokens[0].type, TokenType::EOF_TOKEN);
}

TEST(whitespace_only) {
    auto tokens = scan("   \n\t  \n  ");
    ASSERT_EQ(tokens.size(), 1u);
    ASSERT_EQ(tokens[0].type, TokenType::EOF_TOKEN);
}

TEST(line_tracking) {
    auto tokens = scan("a\nb\nc");
    ASSERT_EQ(tokens.size(), 4u);
    ASSERT_EQ(tokens[0].line, 1);
    ASSERT_EQ(tokens[1].line, 2);
    ASSERT_EQ(tokens[2].line, 3);
}

TEST(column_tracking) {
    auto tokens = scan("  ab cd");
    ASSERT_EQ(tokens.size(), 3u);
    ASSERT_EQ(tokens[0].column, 3); // "ab" starts at col 3
    ASSERT_EQ(tokens[1].column, 6); // "cd" starts at col 6
}

// ── Bitwise and shift operators ──

TEST(bitwise_operators) {
    auto tokens = scan("| & ^ ~");
    ASSERT_EQ(tokens.size(), 5u); // 4 operators + EOF
    ASSERT_EQ(tokens[0].type, TokenType::PIPE);
    ASSERT_EQ(tokens[1].type, TokenType::AMPERSAND);
    ASSERT_EQ(tokens[2].type, TokenType::CARET);
    ASSERT_EQ(tokens[3].type, TokenType::TILDE);
}

TEST(shift_operators) {
    auto tokens = scan("<< >>");
    ASSERT_EQ(tokens.size(), 3u); // 2 operators + EOF
    ASSERT_EQ(tokens[0].type, TokenType::LSHIFT);
    ASSERT_EQ(tokens[1].type, TokenType::RSHIFT);
}

// ── Special operators ──

TEST(nil_coalescing_operator) {
    auto tokens = scan("??");
    ASSERT_EQ(tokens.size(), 2u);
    ASSERT_EQ(tokens[0].type, TokenType::QUESTION_QUESTION);
}

TEST(range_operators) {
    auto tokens = scan(".. ...");
    ASSERT_EQ(tokens.size(), 3u); // 2 operators + EOF
    ASSERT_EQ(tokens[0].type, TokenType::DOT_DOT);
    ASSERT_EQ(tokens[1].type, TokenType::DOT_DOT_DOT);
}

TEST(safe_navigation_operator) {
    auto tokens = scan("?.");
    ASSERT_EQ(tokens.size(), 2u);
    ASSERT_EQ(tokens[0].type, TokenType::QUESTION_DOT);
}

TEST(percent_equal) {
    // % is its own token; %= is not a recognized two-char operator.
    // Ensure % followed by = produces PERCENT then EQUAL.
    auto tokens = scan("% =");
    ASSERT_EQ(tokens.size(), 3u);
    ASSERT_EQ(tokens[0].type, TokenType::PERCENT);
    ASSERT_EQ(tokens[1].type, TokenType::EQUAL);
}

// ── Additional keywords ──

TEST(class_keywords) {
    auto tokens = scan("class this inherits super");
    ASSERT_EQ(tokens.size(), 5u);
    ASSERT_EQ(tokens[0].type, TokenType::CLASS);
    ASSERT_EQ(tokens[1].type, TokenType::THIS);
    ASSERT_EQ(tokens[2].type, TokenType::INHERITS);
    ASSERT_EQ(tokens[3].type, TokenType::SUPER);
}

TEST(trait_contract_keywords) {
    auto tokens = scan("trait uses contract signs");
    ASSERT_EQ(tokens.size(), 5u);
    ASSERT_EQ(tokens[0].type, TokenType::TRAIT);
    ASSERT_EQ(tokens[1].type, TokenType::USES);
    ASSERT_EQ(tokens[2].type, TokenType::CONTRACT);
    ASSERT_EQ(tokens[3].type, TokenType::SIGNS);
}

TEST(access_modifiers) {
    auto tokens = scan("private public static export");
    ASSERT_EQ(tokens.size(), 5u);
    ASSERT_EQ(tokens[0].type, TokenType::PRIVATE);
    ASSERT_EQ(tokens[1].type, TokenType::PUBLIC);
    ASSERT_EQ(tokens[2].type, TokenType::STATIC);
    ASSERT_EQ(tokens[3].type, TokenType::EXPORT);
}

TEST(flow_control_keywords) {
    auto tokens = scan("break continue throw try catch nil");
    ASSERT_EQ(tokens.size(), 7u);
    ASSERT_EQ(tokens[0].type, TokenType::BREAK);
    ASSERT_EQ(tokens[1].type, TokenType::CONTINUE);
    ASSERT_EQ(tokens[2].type, TokenType::THROW);
    ASSERT_EQ(tokens[3].type, TokenType::TRY);
    ASSERT_EQ(tokens[4].type, TokenType::CATCH);
    ASSERT_EQ(tokens[5].type, TokenType::NIL);
}

TEST(type_decl_keywords) {
    auto tokens = scan("data enum match case foreign intrinsic union");
    ASSERT_EQ(tokens.size(), 8u);
    ASSERT_EQ(tokens[0].type, TokenType::DATA);
    ASSERT_EQ(tokens[1].type, TokenType::ENUM);
    ASSERT_EQ(tokens[2].type, TokenType::MATCH);
    ASSERT_EQ(tokens[3].type, TokenType::CASE);
    ASSERT_EQ(tokens[4].type, TokenType::FOREIGN);
    ASSERT_EQ(tokens[5].type, TokenType::INTRINSIC);
    ASSERT_EQ(tokens[6].type, TokenType::UNION);
}

TEST(misc_keywords) {
    auto tokens = scan("attach from const orif as");
    ASSERT_EQ(tokens.size(), 6u);
    ASSERT_EQ(tokens[0].type, TokenType::ATTACH);
    ASSERT_EQ(tokens[1].type, TokenType::FROM);
    ASSERT_EQ(tokens[2].type, TokenType::CONST);
    ASSERT_EQ(tokens[3].type, TokenType::ORIF);
    ASSERT_EQ(tokens[4].type, TokenType::AS);
}

TEST(bool_literals) {
    auto tokens = scan("true false");
    ASSERT_EQ(tokens.size(), 3u);
    ASSERT_EQ(tokens[0].type, TokenType::TRUE);
    ASSERT_EQ(tokens[1].type, TokenType::FALSE);
}

TEST(return_keyword) {
    auto tokens = scan("return");
    ASSERT_EQ(tokens.size(), 2u);
    ASSERT_EQ(tokens[0].type, TokenType::RETURN);
}

// ── Additional literal edge cases ──

TEST(zero_literal) {
    auto tokens = scan("0");
    ASSERT_EQ(tokens.size(), 2u);
    ASSERT_EQ(tokens[0].type, TokenType::NUMBER_INT);
    ASSERT_EQ(tokens[0].lexeme, "0");
}

TEST(large_integer) {
    auto tokens = scan("999999999999");
    ASSERT_EQ(tokens.size(), 2u);
    ASSERT_EQ(tokens[0].type, TokenType::NUMBER_INT);
}

TEST(hex_with_lowercase) {
    auto tokens = scan("0xdeadbeef");
    ASSERT_EQ(tokens.size(), 2u);
    ASSERT_EQ(tokens[0].type, TokenType::NUMBER_INT);
    ASSERT_EQ(tokens[0].lexeme, "0xdeadbeef");
}

TEST(hex_with_uppercase) {
    auto tokens = scan("0xDEADBEEF");
    ASSERT_EQ(tokens.size(), 2u);
    ASSERT_EQ(tokens[0].type, TokenType::NUMBER_INT);
    ASSERT_EQ(tokens[0].lexeme, "0xDEADBEEF");
}

TEST(hex_with_underscores) {
    auto tokens = scan("0xFF_FF");
    ASSERT_EQ(tokens.size(), 2u);
    ASSERT_EQ(tokens[0].type, TokenType::NUMBER_INT);
    ASSERT_EQ(tokens[0].lexeme, "0xFF_FF");
}

TEST(binary_with_underscores) {
    auto tokens = scan("0b1010_1100");
    ASSERT_EQ(tokens.size(), 2u);
    ASSERT_EQ(tokens[0].type, TokenType::NUMBER_INT);
    ASSERT_EQ(tokens[0].lexeme, "0b1010_1100");
}

TEST(float_with_many_decimals) {
    auto tokens = scan("3.141592653589");
    ASSERT_EQ(tokens.size(), 2u);
    ASSERT_EQ(tokens[0].type, TokenType::NUMBER_FLOAT);
}

TEST(empty_string) {
    auto tokens = scan("\"\"");
    ASSERT_EQ(tokens.size(), 2u);
    ASSERT_EQ(tokens[0].type, TokenType::STRING);
}

TEST(string_with_single_quote) {
    auto tokens = scan("\"it's a test\"");
    ASSERT_EQ(tokens.size(), 2u);
    ASSERT_EQ(tokens[0].type, TokenType::STRING);
}

TEST(string_with_escaped_backslash) {
    auto tokens = scan("\"path\\\\to\\\\file\"");
    ASSERT_EQ(tokens.size(), 2u);
    ASSERT_EQ(tokens[0].type, TokenType::STRING);
}

TEST(string_with_hex_escape) {
    auto tokens = scan("\"\\x41\"");
    ASSERT_EQ(tokens.size(), 2u);
    ASSERT_EQ(tokens[0].type, TokenType::STRING);
}

TEST(triple_quoted_multiline) {
    auto tokens = scan("\"\"\"\nline1\nline2\n\"\"\"");
    ASSERT_EQ(tokens.size(), 2u);
    ASSERT_EQ(tokens[0].type, TokenType::STRING);
}

// ── Comment edge cases ──

TEST(nested_block_comment) {
    auto tokens = scan("x /* outer /* inner */ still outer */ y");
    ASSERT_EQ(tokens.size(), 3u); // x, y, EOF
    ASSERT_EQ(tokens[0].lexeme, "x");
    ASSERT_EQ(tokens[1].lexeme, "y");
}

TEST(block_comment_spanning_lines) {
    auto tokens = scan("a /* line1\nline2\nline3 */ b");
    ASSERT_EQ(tokens.size(), 3u); // a, b, EOF
    ASSERT_EQ(tokens[0].lexeme, "a");
    ASSERT_EQ(tokens[1].lexeme, "b");
    // b should be on line 3 (after the comment spanning 3 lines)
    ASSERT_EQ(tokens[1].line, 3);
}

TEST(line_comment_at_end_of_file) {
    auto tokens = scan("x // comment");
    ASSERT_EQ(tokens.size(), 2u); // x, EOF
    ASSERT_EQ(tokens[0].lexeme, "x");
}

TEST(comment_between_tokens) {
    auto tokens = scan("a/* comment */b");
    ASSERT_EQ(tokens.size(), 3u); // a, b, EOF
    ASSERT_EQ(tokens[0].lexeme, "a");
    ASSERT_EQ(tokens[1].lexeme, "b");
}

// ── Position tracking edge cases ──

TEST(multiline_position_tracking) {
    auto tokens = scan("a\n  b\n    c");
    ASSERT_EQ(tokens.size(), 4u);
    ASSERT_EQ(tokens[0].line, 1);
    ASSERT_EQ(tokens[0].column, 1);
    ASSERT_EQ(tokens[1].line, 2);
    ASSERT_EQ(tokens[1].column, 3);
    ASSERT_EQ(tokens[2].line, 3);
    ASSERT_EQ(tokens[2].column, 5);
}

// ── Complex token sequences ──

TEST(mixed_operators_and_literals) {
    auto tokens = scan("let x as i64 = 42 + 3.14");
    ASSERT_EQ(tokens.size(), 9u); // let x as i64 = 42 + 3.14 EOF
    ASSERT_EQ(tokens[0].type, TokenType::LET);
    ASSERT_EQ(tokens[1].type, TokenType::IDENTIFIER);
    ASSERT_EQ(tokens[2].type, TokenType::AS);
    ASSERT_EQ(tokens[3].type, TokenType::IDENTIFIER);
    ASSERT_EQ(tokens[4].type, TokenType::EQUAL);
    ASSERT_EQ(tokens[5].type, TokenType::NUMBER_INT);
    ASSERT_EQ(tokens[6].type, TokenType::PLUS);
    ASSERT_EQ(tokens[7].type, TokenType::NUMBER_FLOAT);
    ASSERT_EQ(tokens[8].type, TokenType::EOF_TOKEN);
}

TEST(function_declaration_tokens) {
    auto tokens = scan("func add(a as i64, b as i64) -> i64");
    ASSERT_EQ(tokens[0].type, TokenType::FUNC);
    ASSERT_EQ(tokens[1].type, TokenType::IDENTIFIER); // add
    ASSERT_EQ(tokens[11].type, TokenType::MINUS_GREATER); // ->
}

TEST(class_declaration_tokens) {
    auto tokens = scan("class Dog inherits Animal signs Named uses Printable");
    ASSERT_EQ(tokens[0].type, TokenType::CLASS);
    ASSERT_EQ(tokens[2].type, TokenType::INHERITS);
    ASSERT_EQ(tokens[4].type, TokenType::SIGNS);
    ASSERT_EQ(tokens[6].type, TokenType::USES);
}

TEST(operator_disambiguation) {
    // Ensure < is not confused with <<, and > is not confused with >>
    auto tokens = scan("< <= << > >= >>");
    ASSERT_EQ(tokens.size(), 7u);
    ASSERT_EQ(tokens[0].type, TokenType::LESS);
    ASSERT_EQ(tokens[1].type, TokenType::LESS_EQUAL);
    ASSERT_EQ(tokens[2].type, TokenType::LSHIFT);
    ASSERT_EQ(tokens[3].type, TokenType::GREATER);
    ASSERT_EQ(tokens[4].type, TokenType::GREATER_EQUAL);
    ASSERT_EQ(tokens[5].type, TokenType::RSHIFT);
}

// MAIN() is defined in test_main.cpp
