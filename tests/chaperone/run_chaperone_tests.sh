#!/bin/bash
# ═══════════════════════════════════════════════════════════════
# Angara Chaperone Test Suite Runner
#
# Verifies the compile-time memory-safety pass (the Chaperone).
#
#   positive/  — clean programs that MUST compile and run.
#   negative/  — buggy programs that MUST trigger a specific Chaperone
#                diagnostic. Each file declares the expected code in a
#                header comment:  `// expect: E502`
#
# Negative-test semantics (post-S0): Chaperone *errors* (E5xx) must halt
# compilation so no binary ships; the harness asserts the code appears in
# compiler output AND no binary is produced. W-codes are warnings and must
# NOT halt compilation.
#
# NOTE: intentionally no `set -e`. A single program hitting a pre-existing
# codegen bug (e.g. the closure/LLVM-verify issue in 03_functions) must not
# abort the whole run — we want a full report.
#
# Usage: ./run_chaperone_tests.sh [compiler_path]
# ═══════════════════════════════════════════════════════════════

RED='\033[1;31m'
GREEN='\033[1;32m'
YELLOW='\033[1;33m'
CYAN='\033[1;36m'
BOLD='\033[1m'
DIM='\033[2;37m'
RESET='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
ANGC="${1:-$PROJECT_DIR/build/angc}"
TMPDIR_TEST="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_TEST"' EXIT

PASS=0
FAIL=0
FAILURES=()

strip_ansi() { sed 's/\x1b\[[0-9;]*m//g'; }

echo ""
printf "${CYAN}════════════════════════════════════════════════════════${RESET}\n"
printf "${CYAN}  Angara Chaperone Memory-Safety Test Suite${RESET}\n"
printf "${CYAN}  Compiler: $ANGC${RESET}\n"
printf "${CYAN}════════════════════════════════════════════════════════${RESET}\n"

if [ ! -f "$ANGC" ]; then
    printf "${RED}Error: Compiler not found at $ANGC${RESET}\n"
    printf "${YELLOW}Run 'make' first to build the compiler.${RESET}\n"
    exit 1
fi

# ─── Positive tests: must compile AND run with exit 0 ─────────────
printf "\n${BOLD}${CYAN}── Positive Tests (clean programs: must compile & run) ──${RESET}\n\n"

for test_file in "$SCRIPT_DIR/positive/"*.an; do
    [ -f "$test_file" ] || continue
    test_name="$(basename "$test_file" .an)"
    binary="$TMPDIR_TEST/${test_name}"
    rm -f "$binary"

    # Read the expected runtime exit code from `// exit: N` (default 0).
    # Positive tests assert behaviour via their exit code, so non-zero is fine
    # when declared — the assertion is "matches the declared value".
    expect_exit=$(strip_ansi < "$test_file" | /bin/grep -oE "exit: -?[0-9]+" | head -1 | awk '{print $2}')
    [ -z "$expect_exit" ] && expect_exit=0

    printf "  ${BOLD}${test_name}${RESET} ${DIM}(exit $expect_exit)${RESET}: "

    compile_output=$("$ANGC" "$test_file" -o "$binary" 2>&1)
    compile_rc=$?

    if [ $compile_rc -ne 0 ] || [ ! -f "$binary" ]; then
        # Distinguish a Chaperone false-positive from a real compile error.
        if echo "$compile_output" | strip_ansi | /bin/grep -qiE "E50[0-9]|E506"; then
            printf "${RED}FALSE POSITIVE${RESET} (Chaperone flagged clean code)\n"
            FAILURES+=("$test_name: Chaperone false positive on clean program")
        else
            printf "${RED}COMPILE ERROR${RESET}\n"
            FAILURES+=("$test_name: Unexpected compile error (non-Chaperone)")
        fi
        echo "$compile_output" | strip_ansi | /bin/grep -iE "error" | head -2 | sed 's/^/         /'
        FAIL=$((FAIL + 1))
        continue
    fi

    "$binary" >/dev/null 2>&1
    run_rc=$?
    if [ "$run_rc" != "$expect_exit" ]; then
        printf "${RED}RUNTIME FAIL${RESET} (exit $run_rc, expected $expect_exit)\n"
        FAILURES+=("$test_name: Runtime exited $run_rc, expected $expect_exit")
        FAIL=$((FAIL + 1))
        continue
    fi

    printf "${GREEN}PASS${RESET}\n"
    PASS=$((PASS + 1))
done

# ─── Negative tests: must trigger the expected diagnostic code ────
printf "\n${BOLD}${CYAN}── Negative Tests (must trigger a specific Chaperone code) ──${RESET}\n\n"

for test_file in "$SCRIPT_DIR/negative/"*.an; do
    [ -f "$test_file" ] || continue
    test_name="$(basename "$test_file" .an)"
    binary="$TMPDIR_TEST/${test_name}"
    rm -f "$binary"

    # Read the expected code from the `// expect: Exxx` header.
    expected=$(strip_ansi < "$test_file" | /bin/grep -oE "expect: [A-Z][0-9]+" | head -1 | awk '{print $2}')
    if [ -z "$expected" ]; then
        printf "  ${BOLD}${test_name}${RESET}: ${RED}NO EXPECT${RESET} (missing '// expect: Exxx' header)\n"
        FAILURES+=("$test_name: missing '// expect:' header")
        FAIL=$((FAIL + 1))
        continue
    fi

    printf "  ${BOLD}${test_name}${RESET} ${DIM}(expect $expected)${RESET}: "

    compile_output=$("$ANGC" "$test_file" -o "$binary" 2>&1)
    compile_rc=$?
    clean=$(echo "$compile_output" | strip_ansi)

    if ! echo "$clean" | /bin/grep -qE "\\b${expected}\\b"; then
        printf "${RED}MISSED${RESET} (expected $expected, not in output)\n"
        echo "$clean" | /bin/grep -iE "error|warning" | head -2 | sed 's/^/         /'
        FAILURES+=("$test_name: expected $expected but it did not fire")
        FAIL=$((FAIL + 1))
        continue
    fi

    # The code fired. Now check enforcement severity:
    #   - E-codes must halt compilation (no binary produced).
    #   - W-codes must NOT halt (binary produced, rc 0).
    case "$expected" in
        E*)
            if [ -f "$binary" ]; then
                printf "${RED}NOT ENFORCED${RESET} ($expected fired but binary shipped)\n"
                FAILURES+=("$test_name: $expected fired but compilation was not halted (S0 regression)")
                FAIL=$((FAIL + 1))
            else
                printf "${GREEN}CAUGHT${RESET} $expected (compile halted)\n"
                PASS=$((PASS + 1))
            fi
            ;;
        W*)
            if [ ! -f "$binary" ]; then
                printf "${YELLOW}CAUGHT${RESET} $expected ${YELLOW}(warning, but compile halted — over-strict)${RESET}\n"
                FAILURES+=("$test_name: $expected is a warning but halted compilation")
                FAIL=$((FAIL + 1))
            else
                printf "${GREEN}CAUGHT${RESET} $expected (warned, compiled)\n"
                PASS=$((PASS + 1))
            fi
            ;;
    esac
done

# ─── Report ───────────────────────────────────────────────────────
if [ ${#FAILURES[@]} -gt 0 ]; then
    printf "\n${BOLD}${YELLOW}── Failures ──${RESET}\n\n"
    i=1
    for f in "${FAILURES[@]}"; do
        printf "  ${YELLOW}%2d. %s${RESET}\n" "$i" "$f"
        i=$((i + 1))
    done
fi

TOTAL=$((PASS + FAIL))
printf "\n${CYAN}════════════════════════════════════════════════════════${RESET}\n"
printf "  ${BOLD}Chaperone results: ${GREEN}${PASS} passed${RESET}"
if [ $FAIL -gt 0 ]; then
    printf ", ${RED}${FAIL} failed${RESET}"
fi
printf " out of ${TOTAL} tests\n"
printf "${CYAN}════════════════════════════════════════════════════════${RESET}\n\n"

exit $FAIL
