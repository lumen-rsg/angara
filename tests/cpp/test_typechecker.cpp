//
// Pipeline-level unit tests for the TypeChecker.
//
// Each test provides a minimal Angara program as a string, runs it through
// Lex → Parse → TypeCheck, and verifies the outcome (success or expected
// error codes).  These tests exercise the TypeChecker's expression/statement
// visitors, type inference, and error reporting through real ASTs — not
// mocked data.
//

#include "test_pipeline.h"

using namespace angara::test;

// ── Positive tests: programs that should type-check cleanly ──

TEST(tc_literal_i64) {
    PipelineHarness h(R"(
        func main() {
            let x = 42;
        }
    )");
    ASSERT_TRUE(h.parse());
    ASSERT_TRUE(h.typeCheck());
    ASSERT_EQ(h.errorCount(), 0);
}

TEST(tc_literal_string) {
    PipelineHarness h(R"(
        func main() {
            let x = "hello";
        }
    )");
    ASSERT_TRUE(h.parse());
    ASSERT_TRUE(h.typeCheck());
    ASSERT_EQ(h.errorCount(), 0);
}

TEST(tc_literal_bool_true) {
    PipelineHarness h(R"(
        func main() {
            let x = true;
        }
    )");
    ASSERT_TRUE(h.parse());
    ASSERT_TRUE(h.typeCheck());
    ASSERT_EQ(h.errorCount(), 0);
}

TEST(tc_literal_bool_false) {
    PipelineHarness h(R"(
        func main() {
            let x = false;
        }
    )");
    ASSERT_TRUE(h.parse());
    ASSERT_TRUE(h.typeCheck());
    ASSERT_EQ(h.errorCount(), 0);
}

TEST(tc_literal_float) {
    PipelineHarness h(R"(
        func main() {
            let x = 3.14;
        }
    )");
    ASSERT_TRUE(h.parse());
    ASSERT_TRUE(h.typeCheck());
    ASSERT_EQ(h.errorCount(), 0);
}

TEST(tc_literal_nil) {
    PipelineHarness h(R"(
        func main() {
            let x = nil;
        }
    )");
    ASSERT_TRUE(h.parse());
    ASSERT_TRUE(h.typeCheck());
    ASSERT_EQ(h.errorCount(), 0);
}

TEST(tc_binary_add_integers) {
    PipelineHarness h(R"(
        func main() {
            let x = 1 + 2;
        }
    )");
    ASSERT_TRUE(h.parse());
    ASSERT_TRUE(h.typeCheck());
    ASSERT_EQ(h.errorCount(), 0);
}

TEST(tc_binary_concat_strings) {
    PipelineHarness h(R"(
        func main() {
            let x = "a" + "b";
        }
    )");
    ASSERT_TRUE(h.parse());
    ASSERT_TRUE(h.typeCheck());
    ASSERT_EQ(h.errorCount(), 0);
}

TEST(tc_binary_compare_integers) {
    PipelineHarness h(R"(
        func main() {
            let x = 1 < 2;
            let y = 1 == 2;
            let z = 1 != 2;
        }
    )");
    ASSERT_TRUE(h.parse());
    ASSERT_TRUE(h.typeCheck());
    ASSERT_EQ(h.errorCount(), 0);
}

TEST(tc_binary_logical) {
    PipelineHarness h(R"(
        func main() {
            let x = true && false;
            let y = true || false;
        }
    )");
    ASSERT_TRUE(h.parse());
    ASSERT_TRUE(h.typeCheck());
    ASSERT_EQ(h.errorCount(), 0);
}

TEST(tc_unary_negate) {
    PipelineHarness h(R"(
        func main() {
            let x = -42;
        }
    )");
    ASSERT_TRUE(h.parse());
    ASSERT_TRUE(h.typeCheck());
    ASSERT_EQ(h.errorCount(), 0);
}

TEST(tc_unary_not) {
    PipelineHarness h(R"(
        func main() {
            let x = !true;
        }
    )");
    ASSERT_TRUE(h.parse());
    ASSERT_TRUE(h.typeCheck());
    ASSERT_EQ(h.errorCount(), 0);
}

TEST(tc_multiple_variables) {
    PipelineHarness h(R"(
        func main() {
            let a = 1;
            let b = 2;
            let c = a + b;
        }
    )");
    ASSERT_TRUE(h.parse());
    ASSERT_TRUE(h.typeCheck());
    ASSERT_EQ(h.errorCount(), 0);
}

TEST(tc_return_value) {
    PipelineHarness h(R"(
        func main() -> i64 {
            return 42;
        }
    )");
    ASSERT_TRUE(h.parse());
    ASSERT_TRUE(h.typeCheck());
    ASSERT_EQ(h.errorCount(), 0);
}

TEST(tc_return_nil_implicit) {
    PipelineHarness h(R"(
        func main() {
            let x = 1;
        }
    )");
    ASSERT_TRUE(h.parse());
    ASSERT_TRUE(h.typeCheck());
    ASSERT_EQ(h.errorCount(), 0);
}

TEST(tc_if_statement) {
    PipelineHarness h(R"(
        func main() {
            let x = true;
            if (x) {
                let y = 1;
            }
        }
    )");
    ASSERT_TRUE(h.parse());
    ASSERT_TRUE(h.typeCheck());
    ASSERT_EQ(h.errorCount(), 0);
}

TEST(tc_if_else_statement) {
    PipelineHarness h(R"(
        func main() {
            let x = true;
            if (x) {
                let y = 1;
            } else {
                let y = 2;
            }
        }
    )");
    ASSERT_TRUE(h.parse());
    ASSERT_TRUE(h.typeCheck());
    ASSERT_EQ(h.errorCount(), 0);
}

TEST(tc_while_loop) {
    PipelineHarness h(R"(
        func main() {
            let i = 0;
            while (i < 10) {
                i = i + 1;
            }
        }
    )");
    ASSERT_TRUE(h.parse());
    ASSERT_TRUE(h.typeCheck());
    ASSERT_EQ(h.errorCount(), 0);
}

TEST(tc_call_builtin_println) {
    PipelineHarness h(R"(
        func main() {
            println("hello");
        }
    )");
    ASSERT_TRUE(h.parse());
    ASSERT_TRUE(h.typeCheck());
    ASSERT_EQ(h.errorCount(), 0);
}

TEST(tc_empty_program) {
    PipelineHarness h("");
    ASSERT_TRUE(h.parse());
    ASSERT_TRUE(h.typeCheck());
    ASSERT_EQ(h.errorCount(), 0);
}

// ── Negative tests: programs that should produce type errors ──

TEST(tc_error_undefined_variable) {
    PipelineHarness h(R"(
        func main() {
            let x = y;
        }
    )");
    ASSERT_TRUE(h.parse());
    h.typeCheck();  // expected to fail
    ASSERT_TRUE(h.errorCount() > 0);  // should have reported undefined 'y'
}

TEST(tc_error_type_mismatch_return) {
    PipelineHarness h(R"(
        func main() -> i64 {
            return "not an int";
        }
    )");
    ASSERT_TRUE(h.parse());
    h.typeCheck();
    ASSERT_TRUE(h.errorCount() > 0);  // type mismatch
}

TEST(tc_error_redeclared_variable) {
    PipelineHarness h(R"(
        func main() {
            let x = 1;
            let x = 2;
        }
    )");
    ASSERT_TRUE(h.parse());
    h.typeCheck();
    ASSERT_TRUE(h.errorCount() > 0);  // redeclaration in same scope
}

// ── Edge cases ──

TEST(tc_shadowing_allowed) {
    PipelineHarness h(R"(
        func main() {
            let x = 1;
            if (true) {
                let x = "shadowed";  // shadowing in nested scope: ok
            }
        }
    )");
    ASSERT_TRUE(h.parse());
    ASSERT_TRUE(h.typeCheck());
    ASSERT_EQ(h.errorCount(), 0);
}

TEST(tc_nested_blocks) {
    PipelineHarness h(R"(
        func main() {
            let a = 1;
            {
                let b = 2;
                let c = a + b;
            }
            let d = a + 1;
        }
    )");
    ASSERT_TRUE(h.parse());
    ASSERT_TRUE(h.typeCheck());
    ASSERT_EQ(h.errorCount(), 0);
}

TEST(tc_grouping_expression) {
    PipelineHarness h(R"(
        func main() {
            let x = (1 + 2) * 3;
        }
    )");
    ASSERT_TRUE(h.parse());
    ASSERT_TRUE(h.typeCheck());
    ASSERT_EQ(h.errorCount(), 0);
}
