//
// Unit tests for StringUtils: levenshtein_distance and shell_escape.
//

#include "test_harness.h"
#include "StringUtils.h"

using namespace angara;

// ── levenshtein_distance ──

TEST(levenshtein_identical_zero) {
    ASSERT_EQ(levenshtein_distance("hello", "hello"), 0u);
}

TEST(levenshtein_empty_strings) {
    ASSERT_EQ(levenshtein_distance("", ""), 0u);
}

TEST(levenshtein_empty_vs_nonempty) {
    ASSERT_EQ(levenshtein_distance("", "abc"), 3u);
    ASSERT_EQ(levenshtein_distance("abc", ""), 3u);
}

TEST(levenshtein_single_insertion) {
    // "cat" -> "cats" = 1 insertion
    ASSERT_EQ(levenshtein_distance("cat", "cats"), 1u);
}

TEST(levenshtein_single_deletion) {
    // "cats" -> "cat" = 1 deletion
    ASSERT_EQ(levenshtein_distance("cats", "cat"), 1u);
}

TEST(levenshtein_single_substitution) {
    // "cat" -> "bat" = 1 substitution
    ASSERT_EQ(levenshtein_distance("cat", "bat"), 1u);
}

TEST(levenshtein_known_distance) {
    // "kitten" -> "sitting" = 3
    ASSERT_EQ(levenshtein_distance("kitten", "sitting"), 3u);
}

TEST(levenshtein_completely_different) {
    // "abc" -> "xyz" = 3 substitutions
    ASSERT_EQ(levenshtein_distance("abc", "xyz"), 3u);
}

TEST(levenshtein_case_sensitive) {
    ASSERT_EQ(levenshtein_distance("Hello", "hello"), 1u);
}

// ── shell_escape ──

TEST(shell_escape_simple_wraps_in_quotes) {
    ASSERT_EQ(shell_escape("hello"), "'hello'");
}

TEST(shell_escape_empty_string) {
    ASSERT_EQ(shell_escape(""), "''");
}

TEST(shell_escape_contains_single_quote) {
    // "it's" -> 'it'\''s'
    ASSERT_EQ(shell_escape("it's"), "'it'\\''s'");
}

TEST(shell_escape_multiple_single_quotes) {
    // "a'b'c" -> 'a'\''b'\''c'
    ASSERT_EQ(shell_escape("a'b'c"), "'a'\\''b'\\''c'");
}

TEST(shell_escape_spaces_preserved) {
    ASSERT_EQ(shell_escape("hello world"), "'hello world'");
}

TEST(shell_escape_special_chars_preserved) {
    ASSERT_EQ(shell_escape("file.txt"), "'file.txt'");
    ASSERT_EQ(shell_escape("$PATH"), "'$PATH'");
}
