//
// Fuzz tests for the Angara Lexer and Parser.
// Generates random inputs and verifies no crashes, hangs, or assertion failures.
//

#include "test_harness.h"
#include "Lexer.h"
#include "Token.h"
#include "Parser.h"
#include "ErrorHandler.h"

#include <random>
#include <algorithm>
#include <sstream>

using namespace angara;

// ── PRNG setup ──

static std::mt19937& rng() {
    static std::mt19937 r(42); // Fixed seed for reproducibility
    return r;
}

static char randomPrintable() {
    static const char pool[] =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789"
        "!@#$%^&*()-_=+[]{}|;:',.<>?/`~\"\\ \t\n";
    std::uniform_int_distribution<size_t> dist(0, sizeof(pool) - 2);
    return pool[dist(rng())];
}

static std::string randomString(size_t len) {
    std::string s;
    s.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        s += randomPrintable();
    }
    return s;
}

// Generates a random string of bytes (including null and control chars).
static std::string randomBytes(size_t len) {
    std::uniform_int_distribution<int> dist(0, 255);
    std::string s;
    s.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        s += static_cast<char>(dist(rng()));
    }
    return s;
}

// Helper to safely lex source and return whether it completed without crashing.
static bool lexSucceeds(const std::string& source) {
    try {
        ErrorHandler handler(source);
        Lexer lexer(source, std::make_shared<std::string>("fuzz.an"), handler);
        auto tokens = lexer.scanTokens();
        // Should always end with EOF
        return !tokens.empty() && tokens.back().type == TokenType::EOF_TOKEN;
    } catch (...) {
        return false;
    }
}

// Helper to safely lex+parse source.
static bool parseSucceeds(const std::string& source) {
    try {
        ErrorHandler handler(source);
        Lexer lexer(source, std::make_shared<std::string>("fuzz.an"), handler);
        auto tokens = lexer.scanTokens();
        Parser parser(tokens, handler);
        parser.parseStmts();
        return true; // We don't care about errors, just no crash
    } catch (...) {
        return false;
    }
}

// ── Random printable ASCII fuzz ──

TEST(fuzz_lexer_random_short) {
    for (int i = 0; i < 1000; ++i) {
        std::uniform_int_distribution<size_t> lenDist(0, 50);
        auto source = randomString(lenDist(rng()));
        ASSERT_TRUE(lexSucceeds(source));
    }
}

TEST(fuzz_lexer_random_medium) {
    for (int i = 0; i < 200; ++i) {
        std::uniform_int_distribution<size_t> lenDist(50, 500);
        auto source = randomString(lenDist(rng()));
        ASSERT_TRUE(lexSucceeds(source));
    }
}

TEST(fuzz_lexer_random_long) {
    for (int i = 0; i < 20; ++i) {
        std::uniform_int_distribution<size_t> lenDist(1000, 10000);
        auto source = randomString(lenDist(rng()));
        ASSERT_TRUE(lexSucceeds(source));
    }
}

// ── Random raw bytes (including null, control chars) ──

TEST(fuzz_lexer_random_bytes_short) {
    for (int i = 0; i < 500; ++i) {
        std::uniform_int_distribution<size_t> lenDist(0, 30);
        auto source = randomBytes(lenDist(rng()));
        ASSERT_TRUE(lexSucceeds(source));
    }
}

TEST(fuzz_lexer_random_bytes_medium) {
    for (int i = 0; i < 50; ++i) {
        std::uniform_int_distribution<size_t> lenDist(100, 1000);
        auto source = randomBytes(lenDist(rng()));
        ASSERT_TRUE(lexSucceeds(source));
    }
}

// ── Structured fuzz: random token sequences ──

static const char* tokenPool[] = {
    "let", "const", "if", "else", "orif", "for", "while", "in",
    "func", "return", "true", "false", "nil", "class", "this",
    "super", "trait", "uses", "static", "export", "break", "continue",
    "is", "data", "enum", "match", "case", "foreign", "try", "catch",
    "throw", "attach", "from", "inherits", "signs", "contract",
    "+", "-", "*", "/", "%", "=", "==", "!=", "<", ">", "<=", ">=",
    "&&", "||", "!", "++", "--", "+=", "-=", "*=", "/=",
    "<<", ">>", "|", "&", "^", "~", "??", "..", "...", "?.",
    "(", ")", "{", "}", "[", "]", ",", ".", ":", ";", "?", "@", "->",
    "42", "3.14", "0xFF", "0b1010",
    "\"hello\"", "\"\"", "\"\"\"\n multiline\n\"\"\"",
    "foo_bar", "x123", "i64", "f64", "string", "bool",
    "// comment", "/* block */",
};

static std::string randomTokenSequence(int count) {
    std::uniform_int_distribution<size_t> dist(0, sizeof(tokenPool) / sizeof(tokenPool[0]) - 1);
    std::stringstream ss;
    for (int i = 0; i < count; ++i) {
        ss << tokenPool[dist(rng())] << " ";
    }
    return ss.str();
}

TEST(fuzz_lexer_random_token_sequence) {
    for (int i = 0; i < 500; ++i) {
        auto source = randomTokenSequence(20);
        ASSERT_TRUE(lexSucceeds(source));
    }
}

TEST(fuzz_parser_random_token_sequence) {
    for (int i = 0; i < 200; ++i) {
        auto source = randomTokenSequence(30);
        ASSERT_TRUE(parseSucceeds(source));
    }
}

// ── Boundary cases ──

TEST(fuzz_lexer_very_long_identifier) {
    std::string source(100000, 'a');
    ASSERT_TRUE(lexSucceeds(source));
}

TEST(fuzz_lexer_very_long_string) {
    std::string inner(50000, 'x');
    std::string source = "\"" + inner + "\"";
    ASSERT_TRUE(lexSucceeds(source));
}

TEST(fuzz_lexer_very_long_number) {
    std::string source(100000, '7');
    ASSERT_TRUE(lexSucceeds(source));
}

TEST(fuzz_lexer_deeply_nested_comments) {
    std::string source;
    for (int i = 0; i < 500; ++i) {
        source += "/* ";
    }
    source += "x";
    for (int i = 0; i < 500; ++i) {
        source += " */";
    }
    // This should lex (x + EOF), even if there are unterminated comment errors
    ASSERT_TRUE(lexSucceeds(source));
}

TEST(fuzz_lexer_many_strings) {
    std::string source;
    for (int i = 0; i < 1000; ++i) {
        source += "\"str" + std::to_string(i) + "\" ";
    }
    ASSERT_TRUE(lexSucceeds(source));
}

TEST(fuzz_lexer_only_operators) {
    std::string source;
    const char ops[] = "+-*/%=<>!&|^~?:;,.@(){}[]";
    for (int i = 0; i < 5000; ++i) {
        std::uniform_int_distribution<size_t> dist(0, sizeof(ops) - 2);
        source += ops[dist(rng())];
    }
    ASSERT_TRUE(lexSucceeds(source));
}

// ── Escape sequence fuzz ──

TEST(fuzz_lexer_random_escapes) {
    for (int i = 0; i < 500; ++i) {
        std::string source = "\"";
        std::uniform_int_distribution<size_t> lenDist(1, 50);
        size_t len = lenDist(rng());
        for (size_t j = 0; j < len; ++j) {
            // Randomly insert backslash + random char
            if (rng()() % 3 == 0) {
                source += '\\';
                source += randomPrintable();
            } else {
                char c = randomPrintable();
                // Avoid unescaped quotes (would terminate string early)
                if (c != '"') source += c;
            }
        }
        source += "\"";
        // This may error on invalid escapes, but should not crash
        ASSERT_TRUE(lexSucceeds(source));
    }
}

// ── Unterminated constructs ──

TEST(fuzz_lexer_unterminated_strings) {
    // Strings without closing quote — should error gracefully
    ASSERT_TRUE(lexSucceeds("\"unterminated"));
    ASSERT_TRUE(lexSucceeds("\""));
    ASSERT_TRUE(lexSucceeds("\"\\"));
}

TEST(fuzz_lexer_unterminated_block_comment) {
    ASSERT_TRUE(lexSucceeds("/* unterminated"));
    ASSERT_TRUE(lexSucceeds("/*"));
    ASSERT_TRUE(lexSucceeds("/* /* nested */"));
}

TEST(fuzz_lexer_unterminated_triple_quote) {
    ASSERT_TRUE(lexSucceeds("\"\"\"unterminated"));
    ASSERT_TRUE(lexSucceeds("\"\"\""));
}

// ── Parser fuzz: random statements ──

TEST(fuzz_parser_random_expressions) {
    const char* exprs[] = {
        "1 + 2;", "a * b - c;", "foo();", "bar(1, 2);",
        "obj.field;", "arr[0];", "true && false;",
        "x || y;", "!a;", "-b;", "(a + b);",
        "nil;", "42;", "3.14;", "\"hello\";",
        "[1, 2, 3];", "x = 1;", "x += 1;",
        "i++;", "--i;", "a ? b : c;",
        "x ?? y;", "a?.b;",
    };
    std::uniform_int_distribution<size_t> countDist(5, 30);
    std::uniform_int_distribution<size_t> exprDist(0, sizeof(exprs) / sizeof(exprs[0]) - 1);

    for (int trial = 0; trial < 200; ++trial) {
        std::string source;
        int count = countDist(rng());
        for (int j = 0; j < count; ++j) {
            source += exprs[exprDist(rng())];
            source += "\n";
        }
        ASSERT_TRUE(parseSucceeds(source));
    }
}

// ── Regression: known tricky inputs ──

TEST(fuzz_lexer_null_bytes) {
    std::string source = "abc";
    source += '\0';
    source += "def";
    ASSERT_TRUE(lexSucceeds(source));
}

TEST(fuzz_lexer_only_null_bytes) {
    std::string source(100, '\0');
    ASSERT_TRUE(lexSucceeds(source));
}

TEST(fuzz_lexer_mixed_line_endings) {
    std::string source = "a\nb\r\nc\rd\n";
    ASSERT_TRUE(lexSucceeds(source));
}

TEST(fuzz_lexer_unicode_bytes) {
    // Random bytes in the 0x80-0xFF range (non-ASCII)
    std::string source;
    std::uniform_int_distribution<int> dist(0x80, 0xFF);
    for (int i = 0; i < 100; ++i) {
        source += static_cast<char>(dist(rng()));
    }
    ASSERT_TRUE(lexSucceeds(source));
}
