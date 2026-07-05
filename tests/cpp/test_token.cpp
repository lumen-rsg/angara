//
// Unit tests for Angara Token and TokenType.
//

#include "test_harness.h"
#include "Token.h"

using namespace angara;

// ── Token construction ──

TEST(default_token) {
    Token t;
    // Default-constructed token should have safe defaults
    ASSERT_EQ(t.lexeme, "");
}

TEST(token_with_fields) {
    auto file = std::make_shared<std::string>("test.an");
    Token t(TokenType::PLUS, "+", 10, 5, file);
    ASSERT_EQ(t.type, TokenType::PLUS);
    ASSERT_EQ(t.lexeme, "+");
    ASSERT_EQ(t.line, 10);
    ASSERT_EQ(t.column, 5);
    ASSERT_TRUE(t.file != nullptr);
    ASSERT_EQ(*t.file, "test.an");
}

TEST(token_without_file) {
    Token t(TokenType::IDENTIFIER, "foo", 1, 1);
    ASSERT_EQ(t.type, TokenType::IDENTIFIER);
    ASSERT_EQ(t.lexeme, "foo");
    ASSERT_TRUE(t.file == nullptr);
}

// ── to_string mapping ──

TEST(to_string_single_char_tokens) {
    ASSERT_EQ(to_string(TokenType::LEFT_PAREN), "LEFT_PAREN");
    ASSERT_EQ(to_string(TokenType::RIGHT_PAREN), "RIGHT_PAREN");
    ASSERT_EQ(to_string(TokenType::LEFT_BRACE), "LEFT_BRACE");
    ASSERT_EQ(to_string(TokenType::RIGHT_BRACE), "RIGHT_BRACE");
    ASSERT_EQ(to_string(TokenType::LEFT_BRACKET), "LEFT_BRACKET");
    ASSERT_EQ(to_string(TokenType::RIGHT_BRACKET), "RIGHT_BRACKET");
    ASSERT_EQ(to_string(TokenType::COMMA), "COMMA");
    ASSERT_EQ(to_string(TokenType::DOT), "DOT");
    ASSERT_EQ(to_string(TokenType::MINUS), "MINUS");
    ASSERT_EQ(to_string(TokenType::PLUS), "PLUS");
    ASSERT_EQ(to_string(TokenType::SLASH), "SLASH");
    ASSERT_EQ(to_string(TokenType::STAR), "STAR");
    ASSERT_EQ(to_string(TokenType::PERCENT), "PERCENT");
    ASSERT_EQ(to_string(TokenType::COLON), "COLON");
    ASSERT_EQ(to_string(TokenType::SEMICOLON), "SEMICOLON");
    ASSERT_EQ(to_string(TokenType::QUESTION), "QUESTION");
    ASSERT_EQ(to_string(TokenType::AT_SIGN), "AT_SIGN");
}

TEST(to_string_two_char_operators) {
    ASSERT_EQ(to_string(TokenType::PLUS_PLUS), "PLUS_PLUS");
    ASSERT_EQ(to_string(TokenType::MINUS_MINUS), "MINUS_MINUS");
    ASSERT_EQ(to_string(TokenType::LOGICAL_AND), "LOGICAL_AND");
    ASSERT_EQ(to_string(TokenType::LOGICAL_OR), "LOGICAL_OR");
    ASSERT_EQ(to_string(TokenType::PLUS_EQUAL), "PLUS_EQUAL");
    ASSERT_EQ(to_string(TokenType::MINUS_EQUAL), "MINUS_EQUAL");
    ASSERT_EQ(to_string(TokenType::STAR_EQUAL), "STAR_EQUAL");
    ASSERT_EQ(to_string(TokenType::SLASH_EQUAL), "SLASH_EQUAL");
    ASSERT_EQ(to_string(TokenType::PERCENT_EQUAL), "PERCENT_EQUAL");
    ASSERT_EQ(to_string(TokenType::AMPERSAND_EQUAL), "AMPERSAND_EQUAL");
    ASSERT_EQ(to_string(TokenType::PIPE_EQUAL), "PIPE_EQUAL");
    ASSERT_EQ(to_string(TokenType::CARET_EQUAL), "CARET_EQUAL");
    ASSERT_EQ(to_string(TokenType::LSHIFT_EQUAL), "LSHIFT_EQUAL");
    ASSERT_EQ(to_string(TokenType::RSHIFT_EQUAL), "RSHIFT_EQUAL");
    ASSERT_EQ(to_string(TokenType::MINUS_GREATER), "MINUS_GREATER");
}

TEST(to_string_comparison_operators) {
    ASSERT_EQ(to_string(TokenType::BANG), "BANG");
    ASSERT_EQ(to_string(TokenType::BANG_EQUAL), "BANG_EQUAL");
    ASSERT_EQ(to_string(TokenType::EQUAL), "EQUAL");
    ASSERT_EQ(to_string(TokenType::EQUAL_EQUAL), "EQUAL_EQUAL");
    ASSERT_EQ(to_string(TokenType::GREATER), "GREATER");
    ASSERT_EQ(to_string(TokenType::GREATER_EQUAL), "GREATER_EQUAL");
    ASSERT_EQ(to_string(TokenType::LESS), "LESS");
    ASSERT_EQ(to_string(TokenType::LESS_EQUAL), "LESS_EQUAL");
}

TEST(to_string_bitwise_operators) {
    ASSERT_EQ(to_string(TokenType::LSHIFT), "LSHIFT");
    ASSERT_EQ(to_string(TokenType::RSHIFT), "RSHIFT");
    ASSERT_EQ(to_string(TokenType::PIPE), "PIPE");
    ASSERT_EQ(to_string(TokenType::AMPERSAND), "AMPERSAND");
    ASSERT_EQ(to_string(TokenType::CARET), "CARET");
    ASSERT_EQ(to_string(TokenType::TILDE), "TILDE");
}

TEST(to_string_special_operators) {
    ASSERT_EQ(to_string(TokenType::QUESTION_QUESTION), "QUESTION_QUESTION");
    ASSERT_EQ(to_string(TokenType::DOT_DOT), "DOT_DOT");
    ASSERT_EQ(to_string(TokenType::DOT_DOT_DOT), "DOT_DOT_DOT");
    ASSERT_EQ(to_string(TokenType::QUESTION_DOT), "QUESTION_DOT");
}

TEST(to_string_literals) {
    ASSERT_EQ(to_string(TokenType::IDENTIFIER), "IDENTIFIER");
    ASSERT_EQ(to_string(TokenType::STRING), "STRING");
    ASSERT_EQ(to_string(TokenType::NUMBER_INT), "NUMBER_INT");
    ASSERT_EQ(to_string(TokenType::NUMBER_FLOAT), "NUMBER_FLOAT");
}

TEST(to_string_keywords) {
    ASSERT_EQ(to_string(TokenType::LET), "LET");
    ASSERT_EQ(to_string(TokenType::CONST), "CONST");
    ASSERT_EQ(to_string(TokenType::IF), "IF");
    ASSERT_EQ(to_string(TokenType::ELSE), "ELSE");
    ASSERT_EQ(to_string(TokenType::FOR), "FOR");
    ASSERT_EQ(to_string(TokenType::WHILE), "WHILE");
    ASSERT_EQ(to_string(TokenType::FUNC), "FUNC");
    ASSERT_EQ(to_string(TokenType::RETURN), "RETURN");
    ASSERT_EQ(to_string(TokenType::CLASS), "CLASS");
    ASSERT_EQ(to_string(TokenType::ENUM), "ENUM");
    ASSERT_EQ(to_string(TokenType::MATCH), "MATCH");
    ASSERT_EQ(to_string(TokenType::DATA), "DATA");
    ASSERT_EQ(to_string(TokenType::TRAIT), "TRAIT");
    ASSERT_EQ(to_string(TokenType::FOREIGN), "FOREIGN");
    ASSERT_EQ(to_string(TokenType::NIL), "NIL");
    ASSERT_EQ(to_string(TokenType::TRUE), "TRUE");
    ASSERT_EQ(to_string(TokenType::FALSE), "FALSE");
    ASSERT_EQ(to_string(TokenType::THIS), "THIS");
    ASSERT_EQ(to_string(TokenType::SUPER), "SUPER");
    ASSERT_EQ(to_string(TokenType::IS), "IS");
    ASSERT_EQ(to_string(TokenType::AS), "AS");
    ASSERT_EQ(to_string(TokenType::ATTACH), "ATTACH");
    ASSERT_EQ(to_string(TokenType::THROW), "THROW");
    ASSERT_EQ(to_string(TokenType::TRY), "TRY");
    ASSERT_EQ(to_string(TokenType::CATCH), "CATCH");
    ASSERT_EQ(to_string(TokenType::BREAK), "BREAK");
    ASSERT_EQ(to_string(TokenType::CONTINUE), "CONTINUE");
}

TEST(to_string_eof) {
    ASSERT_EQ(to_string(TokenType::EOF_TOKEN), "EOF_TOKEN");
}

// ── Token print ──

TEST(token_print_does_not_crash) {
    auto file = std::make_shared<std::string>("test.an");
    Token t(TokenType::PLUS, "+", 1, 1, file);
    // Should not crash or throw
    t.print();
}

TEST(token_print_without_file) {
    Token t(TokenType::IDENTIFIER, "foo", 1, 1);
    t.print();
}
