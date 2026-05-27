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

MAIN()
