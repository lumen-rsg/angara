//
// Pipeline-level unit tests for the Chaperone memory-safety analysis.
//
// Each test provides a minimal Angara program, runs it through
// Lex → Parse → TypeCheck → Chaperone, and verifies the expected
// diagnostic behavior (no errors for correct code, specific error
// codes for known safety violations).
//

#include "test_pipeline.h"

using namespace angara::test;

// ── Positive tests: correct programs that should pass Chaperone ──

TEST(chap_correct_drop) {
    PipelineHarness h(R"(
        class Buf {
            let v as i64;
            func init(this, x as i64) { this.v = x; }
        }

        func main() {
            let b = Buf(42);
            drop b;
        }
    )");
    ASSERT_TRUE(h.parse());
    ASSERT_TRUE(h.typeCheck());
    ASSERT_TRUE(h.runChaperone());
    ASSERT_EQ(h.errorCount(), 0);
}

// ── Negative tests: programs that should trigger Chaperone errors ──

TEST(chap_leak_e501) {
    PipelineHarness h(R"(
        class Buf {
            let v as i64;
            func init(this, x as i64) { this.v = x; }
        }

        func main() {
            let b = Buf(42);
            // missing drop — should report E501
        }
    )");
    ASSERT_TRUE(h.parse());
    ASSERT_TRUE(h.typeCheck());
    h.runChaperone();  // expected to find errors
    ASSERT_TRUE(h.errorCount() > 0);
}

TEST(chap_use_after_free_e502) {
    PipelineHarness h(R"(
        class Buf {
            let v as i64;
            func init(this, x as i64) { this.v = x; }
            public:
            func get(this) -> i64 { return this.v; }
        }

        func main() {
            let b = Buf(42);
            drop b;
            let x = b.get();  // use after free
        }
    )");
    ASSERT_TRUE(h.parse());
    ASSERT_TRUE(h.typeCheck());
    h.runChaperone();
    ASSERT_TRUE(h.errorCount() > 0);
}

TEST(chap_double_drop_e503) {
    PipelineHarness h(R"(
        class Buf {
            let v as i64;
            func init(this, x as i64) { this.v = x; }
        }

        func main() {
            let b = Buf(42);
            drop b;
            drop b;  // double drop
        }
    )");
    ASSERT_TRUE(h.parse());
    ASSERT_TRUE(h.typeCheck());
    h.runChaperone();
    ASSERT_TRUE(h.errorCount() > 0);
}

TEST(chap_no_leak_with_transfer) {
    // Returning the tracked value transfers ownership — no leak.
    PipelineHarness h(R"(
        class Buf {
            let v as i64;
            func init(this, x as i64) { this.v = x; }
        }

        func make() -> Buf {
            let b = Buf(99);
            return b;  // ownership transferred to caller
        }

        func main() {
            let x = make();
            drop x;
        }
    )");
    ASSERT_TRUE(h.parse());
    ASSERT_TRUE(h.typeCheck());
    ASSERT_TRUE(h.runChaperone());
    ASSERT_EQ(h.errorCount(), 0);
}

TEST(chap_no_false_positive_primitive) {
    // Primitives are not tracked — no leak for dropping a bool.
    PipelineHarness h(R"(
        func main() {
            let x = true;
            let y = 42;
            let z = "hello";
        }
    )");
    ASSERT_TRUE(h.parse());
    ASSERT_TRUE(h.typeCheck());
    ASSERT_TRUE(h.runChaperone());
    ASSERT_EQ(h.errorCount(), 0);
}

TEST(chap_drop_in_both_branches) {
    // Both branches of if/else drop the tracked value — correct.
    PipelineHarness h(R"(
        class Buf {
            let v as i64;
            func init(this, x as i64) { this.v = x; }
        }

        func main() {
            let b = Buf(42);
            let cond = true;
            if (cond) {
                drop b;
            } else {
                drop b;
            }
        }
    )");
    ASSERT_TRUE(h.parse());
    ASSERT_TRUE(h.typeCheck());
    ASSERT_TRUE(h.runChaperone());
    ASSERT_EQ(h.errorCount(), 0);
}

TEST(chap_leak_in_one_branch) {
    // Only one branch drops — Chaperone should report a leak.
    PipelineHarness h(R"(
        class Buf {
            let v as i64;
            func init(this, x as i64) { this.v = x; }
        }

        func main() {
            let b = Buf(42);
            let cond = true;
            if (cond) {
                drop b;
            }
            // else branch: b still Live — leak
        }
    )");
    ASSERT_TRUE(h.parse());
    ASSERT_TRUE(h.typeCheck());
    h.runChaperone();
    ASSERT_TRUE(h.errorCount() > 0);
}

TEST(chap_empty_program_no_errors) {
    PipelineHarness h("");
    ASSERT_TRUE(h.parse());
    ASSERT_TRUE(h.typeCheck());
    ASSERT_TRUE(h.runChaperone());
    ASSERT_EQ(h.errorCount(), 0);
}
