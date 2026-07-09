#!/bin/bash
# ═══════════════════════════════════════════════════════════════
# Angara Language Test Suite Runner
#
# Tests positive cases (should compile & run correctly) and
# negative cases (should produce compilation errors).
#
# Failures are reported as BUGS in the compiler.
# Usage: ./run_tests.sh [compiler_path]
# ═══════════════════════════════════════════════════════════════

set -euo pipefail

RED='\033[1;31m'
GREEN='\033[1;32m'
YELLOW='\033[1;33m'
CYAN='\033[1;36m'
DIM='\033[2m'
BOLD='\033[1m'
RESET='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
ANGC="${1:-$PROJECT_DIR/build/angc}"
TMPDIR_TEST="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_TEST"' EXIT

PASS=0
FAIL=0
BUGS=()

strip_ansi() { sed 's/\x1b\[[0-9;]*m//g'; }

echo ""
printf "${CYAN}════════════════════════════════════════════════════════${RESET}\n"
printf "${CYAN}  Angara Language Test Suite${RESET}\n"
printf "${CYAN}  Compiler: $ANGC${RESET}\n"
printf "${CYAN}════════════════════════════════════════════════════════${RESET}\n"
echo ""

if [ ! -f "$ANGC" ]; then
    printf "${RED}Error: Compiler not found at $ANGC${RESET}\n"
    printf "${YELLOW}Run 'make' first to build the compiler.${RESET}\n"
    exit 1
fi


find_binary() {
    local test_name="$1"
    local tmp_path="$2"
    if [ -f "$tmp_path" ]; then
        echo "$tmp_path"
        return
    fi
    local cwd_path="$PROJECT_DIR/$test_name"
    if [ -f "$cwd_path" ]; then
        mv "$cwd_path" "$tmp_path"
        echo "$tmp_path"
        return
    fi
    echo ""
}

printf "${BOLD}${CYAN}── Positive Tests (should compile & run correctly) ──${RESET}\n\n"

# run_positive_test <test_name> <compile_file> <markers_file>
# Compiles <compile_file>, runs the resulting binary, and asserts the `// exit:`
# and `// stdout:` markers read from <markers_file>. Shared by the single-file
# and multi-file (directory) positive loops.
run_positive_test() {
    local test_name="$1"
    local compile_file="$2"
    local markers_file="$3"
    local binary="$TMPDIR_TEST/${test_name}"

    # Read expected runtime exit code from `// exit: N` (default 0).
    # Read expected stdout substrings from `// stdout: <pattern>` (optional, may repeat).
    expect_exit=$(strip_ansi < "$markers_file" | /bin/grep -oE "exit: -?[0-9]+" | head -1 | awk '{print $2}' || true)
    [ -z "$expect_exit" ] && expect_exit=0
    mapfile -t expect_stdout < <(strip_ansi < "$markers_file" | /bin/grep -oP 'stdout:\s*\K.*' || true)

    printf "  ${BOLD}${test_name}${RESET} ${DIM}(exit $expect_exit)${RESET}: "

    compile_output=$("$ANGC" "$compile_file" -o "$binary" 2>&1) && compile_rc=$? || compile_rc=$?

    if [ $compile_rc -ne 0 ]; then
        if echo "$compile_output" | strip_ansi | grep -qi "linker\|Undefined symbol"; then
            printf "${RED}LINKER ERROR${RESET}\n"
            BUGS+=("BUG [$test_name]: Linker error - missing runtime symbols")
        elif echo "$compile_output" | grep -q "SIGABRT\|SIGSEGV\|exception\|terminating"; then
            printf "${RED}CRASH${RESET} (compiler crashed)\n"
            BUGS+=("BUG [$test_name]: Compiler crash during compilation")
        else
            printf "${RED}COMPILE ERROR${RESET}\n"
            BUGS+=("BUG [$test_name]: Unexpected compile error")
        fi
        echo "$compile_output" | strip_ansi | grep -i "error" | head -3 | sed 's/^/         /'
        FAIL=$((FAIL + 1))
        return
    fi

    actual_binary="$(find_binary "$test_name" "$binary")"
    if [ -z "$actual_binary" ]; then
        printf "${RED}NO BINARY${RESET} (compiled but binary not found)\n"
        BUGS+=("BUG [$test_name]: Binary not found after successful compile")
        FAIL=$((FAIL + 1))
        return
    fi

    run_output=$("$actual_binary" 2>&1) && run_rc=$? || run_rc=$?

    # Check exit code against expected.
    if [ "$run_rc" != "$expect_exit" ]; then
        if [ $run_rc -eq 139 ]; then
            printf "${RED}SEGFAULT${RESET} (expected exit $expect_exit)\n"
            BUGS+=("BUG [$test_name]: Runtime segfault")
        elif [ $run_rc -eq 134 ]; then
            printf "${RED}ABORT${RESET} (expected exit $expect_exit)\n"
            BUGS+=("BUG [$test_name]: Runtime abort")
        else
            printf "${RED}WRONG EXIT${RESET} (got $run_rc, expected $expect_exit)\n"
            BUGS+=("BUG [$test_name]: Exit code $run_rc, expected $expect_exit")
        fi
        FAIL=$((FAIL + 1))
        return
    fi

    # Check stdout patterns if any are declared.
    stdout_fail=0
    for pattern in "${expect_stdout[@]}"; do
        if ! echo "$run_output" | grep -qF -- "$pattern"; then
            printf "${RED}STDOUT MISMATCH${RESET} (missing: '%s')\n" "$pattern"
            BUGS+=("BUG [$test_name]: Expected stdout to contain '$pattern'")
            stdout_fail=1
            break
        fi
    done
    if [ $stdout_fail -eq 1 ]; then
        FAIL=$((FAIL + 1))
        return
    fi

    printf "${GREEN}PASS${RESET}\n"
    PASS=$((PASS + 1))
}

# Single-file positive tests.
for test_file in "$SCRIPT_DIR/positive/"*.an; do
    [ -f "$test_file" ] || continue
    test_name="$(basename "$test_file" .an)"
    run_positive_test "$test_name" "$test_file" "$test_file"
done

# Multi-file positive tests. Each subdirectory of positive/ contains a main.an
# entry point plus one or more helper .an files imported via `attach ... from`.
# The entry (main.an) carries the `// exit:` and `// stdout:` markers. Compiling
# main.an triggers discovery and compilation of the imported helpers.
printf "\n${BOLD}${CYAN}── Multi-file Positive Tests (cross-module imports) ──${RESET}\n\n"
for test_dir in "$SCRIPT_DIR/positive/"*/; do
    [ -d "$test_dir" ] || continue
    [ -f "$test_dir/main.an" ] || continue
    test_name="mf_$(basename "$test_dir")"
    run_positive_test "$test_name" "$test_dir/main.an" "$test_dir/main.an"
done

printf "\n${BOLD}${CYAN}── Negative Tests (should produce compilation errors) ──${RESET}\n\n"

for test_file in "$SCRIPT_DIR/negative/"*.an; do
    [ -f "$test_file" ] || continue
    test_name="$(basename "$test_file" .an)"
    binary="$TMPDIR_TEST/${test_name}"

    # Read the expected error code from `// expect: Exxx` header (optional).
    # When present, the test must produce that specific diagnostic code.
    # When absent, fall back to legacy behaviour (any error is acceptable).
    expected=$(strip_ansi < "$test_file" | /bin/grep -oE "expect: [A-Z][0-9]+" | head -1 | awk '{print $2}' || true)

    printf "  ${BOLD}${test_name}${RESET}"
    [ -n "$expected" ] && printf " ${DIM}(expect $expected)${RESET}"
    printf ": "

    compile_output=$("$ANGC" "$test_file" -o "$binary" 2>&1) && compile_rc=$? || compile_rc=$?
    clean=$(echo "$compile_output" | strip_ansi)

    if [ $compile_rc -eq 0 ]; then
        printf "${RED}MISSING ERROR${RESET} (compiled but should have failed)\n"
        BUGS+=("BUG [$test_name]: Type checker failed to catch error")
        FAIL=$((FAIL + 1))
        continue
    fi

    if echo "$compile_output" | grep -q "SIGABRT\|SIGSEGV\|exception\|terminating"; then
        printf "${RED}CRASH${RESET} (crashed instead of reporting error)\n"
        BUGS+=("BUG [$test_name]: Compiler crash instead of error message")
        FAIL=$((FAIL + 1))
        continue
    fi

    if [ -n "$expected" ]; then
        # Specific error-code validation (M23).
        if echo "$clean" | /bin/grep -qE "\\b${expected}\\b"; then
            error_line=$(echo "$clean" | /bin/grep -E "\\b${expected}\\b" | head -1)
            printf "${GREEN}CAUGHT${RESET} %s\n" "$error_line"
            PASS=$((PASS + 1))
        else
            printf "${RED}WRONG CODE${RESET} (expected $expected, not found in output)\n"
            echo "$clean" | /bin/grep -iE "error" | head -2 | sed 's/^/         /'
            BUGS+=("BUG [$test_name]: Expected $expected but it did not appear")
            FAIL=$((FAIL + 1))
        fi
    else
        # Legacy behaviour: accept any error output.
        if echo "$clean" | grep -qi "error"; then
            error_line=$(echo "$clean" | grep -i "error" | head -1)
            printf "${GREEN}CAUGHT${RESET} %s\n" "$error_line"
            PASS=$((PASS + 1))
        else
            printf "${GREEN}CAUGHT${RESET} (non-zero exit)\n"
            PASS=$((PASS + 1))
        fi
    fi
done

if [ ${#BUGS[@]} -gt 0 ]; then
    printf "\n${BOLD}${YELLOW}── Bug Report ──${RESET}\n\n"
    i=1
    for bug in "${BUGS[@]}"; do
        printf "  ${YELLOW}%2d. %s${RESET}\n" "$i" "$bug"
        i=$((i + 1))
    done
fi

TOTAL=$((PASS + FAIL))
printf "\n${CYAN}════════════════════════════════════════════════════════${RESET}\n"
printf "  ${BOLD}Results: ${GREEN}${PASS} passed${RESET}"
if [ $FAIL -gt 0 ]; then
    printf ", ${RED}${FAIL} failed (bugs)${RESET}"
fi
printf " out of ${TOTAL} tests\n"
printf "${CYAN}════════════════════════════════════════════════════════${RESET}\n\n"

exit $FAIL