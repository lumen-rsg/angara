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

# Strip ANSI codes for grepping
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

# ─── Helper ──────────────────────────────────────────────────
# Find the compiled binary (compiler may output to CWD, not -o path)
find_binary() {
    local test_name="$1"
    local tmp_path="$2"
    # Check tmp path first
    if [ -f "$tmp_path" ]; then
        echo "$tmp_path"
        return
    fi
    # Check CWD (project root)
    local cwd_path="$PROJECT_DIR/$test_name"
    if [ -f "$cwd_path" ]; then
        mv "$cwd_path" "$tmp_path"
        echo "$tmp_path"
        return
    fi
    # Check CWD without prefix
    echo ""
}

# ─── POSITIVE TESTS ──────────────────────────────────────────
printf "${BOLD}${CYAN}── Positive Tests (should compile & run correctly) ──${RESET}\n\n"

for test_file in "$SCRIPT_DIR/positive/"*.an; do
    [ -f "$test_file" ] || continue
    test_name="$(basename "$test_file" .an)"
    binary="$TMPDIR_TEST/${test_name}"

    printf "  ${BOLD}${test_name}${RESET}: "

    # Compile
    compile_output=$("$ANGC" "$test_file" -o "$binary" 2>&1) && compile_rc=$? || compile_rc=$?

    if [ $compile_rc -ne 0 ]; then
        # Determine failure type
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
        # Show the error details
        echo "$compile_output" | strip_ansi | grep -i "error" | head -3 | sed 's/^/         /'
        FAIL=$((FAIL + 1))
        continue
    fi

    # Find binary
    actual_binary="$(find_binary "$test_name" "$binary")"
    if [ -z "$actual_binary" ]; then
        printf "${RED}NO BINARY${RESET} (compiled but binary not found)\n"
        BUGS+=("BUG [$test_name]: Binary not found after successful compile")
        FAIL=$((FAIL + 1))
        continue
    fi

    # Run
    run_output=$("$actual_binary" 2>&1) && run_rc=$? || run_rc=$?

    if [ $run_rc -ne 0 ]; then
        if [ $run_rc -eq 139 ]; then
            printf "${RED}SEGFAULT${RESET} (runtime exit 139)\n"
            BUGS+=("BUG [$test_name]: Runtime segfault")
        elif [ $run_rc -eq 134 ]; then
            printf "${RED}ABORT${RESET} (runtime exit 134)\n"
            BUGS+=("BUG [$test_name]: Runtime abort (assertion?)")
        else
            printf "${RED}RUNTIME ERROR${RESET} (exit $run_rc)\n"
            BUGS+=("BUG [$test_name]: Runtime error (exit $run_rc)")
        fi
        FAIL=$((FAIL + 1))
        continue
    fi

    printf "${GREEN}PASS${RESET}\n"
    PASS=$((PASS + 1))
done

# ─── NEGATIVE TESTS ──────────────────────────────────────────
printf "\n${BOLD}${CYAN}── Negative Tests (should produce compilation errors) ──${RESET}\n\n"

for test_file in "$SCRIPT_DIR/negative/"*.an; do
    [ -f "$test_file" ] || continue
    test_name="$(basename "$test_file" .an)"
    binary="$TMPDIR_TEST/${test_name}"

    printf "  ${BOLD}${test_name}${RESET}: "

    compile_output=$("$ANGC" "$test_file" -o "$binary" 2>&1) && compile_rc=$? || compile_rc=$?

    if [ $compile_rc -eq 0 ]; then
        printf "${RED}MISSING ERROR${RESET} (compiled but should have failed)\n"
        BUGS+=("BUG [$test_name]: Type checker failed to catch error")
        FAIL=$((FAIL + 1))
        continue
    fi

    # Check it's an actual error message, not a crash
    if echo "$compile_output" | grep -q "SIGABRT\|SIGSEGV\|exception\|terminating"; then
        printf "${RED}CRASH${RESET} (crashed instead of reporting error)\n"
        BUGS+=("BUG [$test_name]: Compiler crash instead of error message")
        FAIL=$((FAIL + 1))
    elif echo "$compile_output" | strip_ansi | grep -qi "error"; then
        error_line=$(echo "$compile_output" | strip_ansi | grep -i "error" | head -1)
        printf "${GREEN}CAUGHT${RESET} %s\n" "$error_line"
        PASS=$((PASS + 1))
    else
        printf "${GREEN}CAUGHT${RESET} (non-zero exit)\n"
        PASS=$((PASS + 1))
    fi
done

# ─── BUG REPORT ──────────────────────────────────────────────
if [ ${#BUGS[@]} -gt 0 ]; then
    printf "\n${BOLD}${YELLOW}── Bug Report ──${RESET}\n\n"
    i=1
    for bug in "${BUGS[@]}"; do
        printf "  ${YELLOW}%2d. %s${RESET}\n" "$i" "$bug"
        i=$((i + 1))
    done
fi

# ─── SUMMARY ─────────────────────────────────────────────────
TOTAL=$((PASS + FAIL))
printf "\n${CYAN}════════════════════════════════════════════════════════${RESET}\n"
printf "  ${BOLD}Results: ${GREEN}${PASS} passed${RESET}"
if [ $FAIL -gt 0 ]; then
    printf ", ${RED}${FAIL} failed (bugs)${RESET}"
fi
printf " out of ${TOTAL} tests\n"
printf "${CYAN}════════════════════════════════════════════════════════${RESET}\n\n"

exit $FAIL