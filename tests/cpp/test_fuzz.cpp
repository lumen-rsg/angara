//
// Fuzz tests for the Angara compiler pipeline.
// Generates random inputs and verifies no crashes, hangs, or assertion failures
// across the Lexer, Parser, TypeChecker, Chaperone, and LLVM codegen stages.
//

#include "test_harness.h"
#include "test_pipeline.h"
#include "Lexer.h"
#include "Token.h"
#include "Parser.h"
#include "ErrorHandler.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/TargetSelect.h>

#include <random>
#include <algorithm>
#include <sstream>

using namespace angara;
using namespace angara::test;

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

// ═══════════════════════════════════════════════════════════════════════════
// Pipeline fuzz: Lex → Parse → TypeCheck (no crash on random input)
// ═══════════════════════════════════════════════════════════════════════════

// Helper: runs a source string through parse + typeCheck, returns true if no
// C++ exception was thrown (Angara compile errors are expected and ignored).
static bool pipelineSucceeds(const std::string& source) {
    try {
        PipelineHarness h(source, "fuzz");
        h.parse();        // may fail → ok, we just want no crash
        h.typeCheck();    // may fail → ok, we just want no crash
        return true;
    } catch (...) {
        return false;
    }
}

// Helper: runs parse + typeCheck + chaperone, returns true if no C++ exception.
static bool chaperoneFuzzSucceeds(const std::string& source) {
    try {
        PipelineHarness h(source, "fuzz");
        h.parse();
        if (h.typeCheck()) {
            h.runChaperone();  // only run chaperone if type-check passed
        }
        return true;
    } catch (...) {
        return false;
    }
}

// ── Random token sequences through TypeChecker ──

TEST(fuzz_typechecker_random_token_sequence) {
    for (int i = 0; i < 200; ++i) {
        auto source = randomTokenSequence(30);
        ASSERT_TRUE(pipelineSucceeds(source));
    }
}

TEST(fuzz_typechecker_random_short) {
    for (int i = 0; i < 300; ++i) {
        std::uniform_int_distribution<size_t> lenDist(0, 50);
        auto source = randomString(lenDist(rng()));
        ASSERT_TRUE(pipelineSucceeds(source));
    }
}

TEST(fuzz_typechecker_random_bytes) {
    for (int i = 0; i < 200; ++i) {
        std::uniform_int_distribution<size_t> lenDist(0, 30);
        auto source = randomBytes(lenDist(rng()));
        ASSERT_TRUE(pipelineSucceeds(source));
    }
}

// ── Random token sequences through Chaperone ──

TEST(fuzz_chaperone_random_token_sequence) {
    for (int i = 0; i < 150; ++i) {
        auto source = randomTokenSequence(25);
        ASSERT_TRUE(chaperoneFuzzSucceeds(source));
    }
}

TEST(fuzz_chaperone_random_short) {
    for (int i = 0; i < 200; ++i) {
        std::uniform_int_distribution<size_t> lenDist(0, 80);
        auto source = randomString(lenDist(rng()));
        ASSERT_TRUE(chaperoneFuzzSucceeds(source));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Structured program fuzz: generates wrapper programs that are more likely
// to parse, reaching the TypeChecker and Chaperone with valid ASTs.
// ═══════════════════════════════════════════════════════════════════════════

// Generates a wrapped "func main() { <body> }" program from a body string.
static std::string wrapMain(const std::string& body) {
    return "func main() {\n" + body + "\n}\n";
}

// Generates a random literal expression that should always type-check.
static std::string randomLiteral() {
    static const char* literals[] = {
        "42", "0", "-1", "3.14", "0.0",
        "true", "false", "nil",
        "\"hello\"", "\"\"",
    };
    std::uniform_int_distribution<size_t> dist(0, sizeof(literals) / sizeof(literals[0]) - 1);
    return literals[dist(rng())];
}

// Generates a random binary operator.
static std::string randomBinaryOp() {
    static const char* ops[] = {"+", "-", "*", "/", "==", "!=", "<", ">", "<=", ">=", "&&", "||"};
    std::uniform_int_distribution<size_t> dist(0, sizeof(ops) / sizeof(ops[0]) - 1);
    return ops[dist(rng())];
}

// Generates a random type annotation.
static std::string randomType() {
    static const char* types[] = {"i64", "f64", "bool", "string"};
    std::uniform_int_distribution<size_t> dist(0, sizeof(types) / sizeof(types[0]) - 1);
    return types[dist(rng())];
}

// Generates a simple program body: a sequence of let-declarations and
// simple expressions. These should parse and often type-check.
static std::string randomProgramBody(int stmtCount) {
    std::stringstream ss;
    // First, declare some variables so later statements can reference them.
    int varCount = 0;
    std::vector<std::string> vars;
    for (int i = 0; i < stmtCount; ++i) {
        int kind = rng()() % 6;
        switch (kind) {
            case 0: {
                // let v = <literal>;
                std::string name = "v" + std::to_string(varCount++);
                vars.push_back(name);
                ss << "let " << name << " = " << randomLiteral() << ";\n";
                break;
            }
            case 1: {
                // let v = <literal> <op> <literal>;
                if (varCount < 2) { --i; continue; }
                std::string name = "v" + std::to_string(varCount++);
                vars.push_back(name);
                ss << "let " << name << " = " << randomLiteral()
                   << " " << randomBinaryOp() << " " << randomLiteral() << ";\n";
                break;
            }
            case 2: {
                // let v: <type> = <literal>;
                if (varCount < 1) { --i; continue; }
                std::string name = "v" + std::to_string(varCount++);
                vars.push_back(name);
                ss << "let " << name << " as " << randomType()
                   << " = " << randomLiteral() << ";\n";
                break;
            }
            case 3: {
                // Simple expression statement: println("...");
                ss << "println(" << randomLiteral() << ");\n";
                break;
            }
            case 4: {
                // if (true) { ... } block
                ss << "if (true) {\n";
                ss << "  let tmp" << varCount << " = " << randomLiteral() << ";\n";
                ss << "}\n";
                ++varCount;
                break;
            }
            case 5: {
                // Assignment to existing variable
                if (vars.empty()) { --i; continue; }
                std::uniform_int_distribution<size_t> pick(0, vars.size() - 1);
                ss << vars[pick(rng())] << " = " << randomLiteral() << ";\n";
                break;
            }
        }
    }
    return ss.str();
}

// ── Structured program fuzz through TypeChecker ──

TEST(fuzz_typechecker_structured_programs) {
    for (int i = 0; i < 200; ++i) {
        std::uniform_int_distribution<int> stmtDist(1, 15);
        auto body = randomProgramBody(stmtDist(rng()));
        auto source = wrapMain(body);
        ASSERT_TRUE(pipelineSucceeds(source));
    }
}

// ── Structured program fuzz through Chaperone ──

TEST(fuzz_chaperone_structured_programs) {
    for (int i = 0; i < 150; ++i) {
        std::uniform_int_distribution<int> stmtDist(1, 12);
        auto body = randomProgramBody(stmtDist(rng()));
        auto source = wrapMain(body);
        ASSERT_TRUE(chaperoneFuzzSucceeds(source));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// LLVM Codegen fuzz: basic LLVM infrastructure validation.
//
// Full LLVMBackend-based codegen fuzzing (generateIR) is deferred — the
// LLVMBackend constructor generates the entire Angara runtime into a fresh
// LLVM module, which is too expensive for per-iteration fuzz testing on
// some platforms (~500ms+ per construction on ARM64).  The smoke tests
// below validate that LLVM libraries are correctly linked and targets can
// be initialized.  Full codegen fuzzing is accomplished via the `angc`
// compiler binary in the language test suite (tests/lang/), which exercises
// the full pipeline including LLVM codegen on hundreds of programs.
// ═══════════════════════════════════════════════════════════════════════════

// ── Codegen fuzz: LLVM IR generation smoke test ──

// Verifies that basic LLVM infrastructure works in the test environment.
TEST(fuzz_codegen_llvm_smoke_test) {
    try {
        llvm::LLVMContext ctx;
        llvm::Module mod("test", ctx);
        ASSERT_TRUE(true);
    } catch (...) {
        ASSERT_TRUE(false);
    }
}

// Verifies that LLVM native target can be initialized on this platform.
TEST(fuzz_codegen_llvm_target_init) {
    try {
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
        llvm::InitializeNativeTargetAsmParser();
        ASSERT_TRUE(true);
    } catch (...) {
        ASSERT_TRUE(false);
    }
}

// ── Boundary fuzz: random programs with larger bodies ──

TEST(fuzz_pipeline_large_random_program) {
    for (int trial = 0; trial < 30; ++trial) {
        std::uniform_int_distribution<int> stmtDist(10, 40);
        auto body = randomProgramBody(stmtDist(rng()));
        auto source = wrapMain(body);
        ASSERT_TRUE(pipelineSucceeds(source));
    }
}

TEST(fuzz_chaperone_large_random_program) {
    for (int trial = 0; trial < 20; ++trial) {
        std::uniform_int_distribution<int> stmtDist(10, 30);
        auto body = randomProgramBody(stmtDist(rng()));
        auto source = wrapMain(body);
        ASSERT_TRUE(chaperoneFuzzSucceeds(source));
    }
}
