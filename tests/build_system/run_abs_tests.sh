#!/bin/bash
# ═══════════════════════════════════════════════════════════════
# Angara .abs build-system test runner
#
# Verifies that `angc --path project.abs` correctly links native modules
# (Bug 2: must not emit spurious -l<name> flags) and produces a runnable
# binary from a multi-file project that shares a native module across files
# (Bug 1: no "multiple definition" of __ang_<mod>_<fn> wrappers).
#
# Usage: ./run_abs_tests.sh [compiler_path] [angc_repo_root]
# ═══════════════════════════════════════════════════════════════
set -uo pipefail

RED='\033[1;31m'
GREEN='\033[1;32m'
CYAN='\033[1;36m'
DIM='\033[2m'
BOLD='\033[1m'
RESET='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="${2:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
ANGC="${1:-$PROJECT_DIR/build/angc}"

PASS=0
FAIL=0
BUGS=()

strip_ansi() { sed 's/\x1b\[[0-9;]*m//g'; }

printf "\n${CYAN}════════════════════════════════════════════════════════${RESET}\n"
printf "${CYAN}  Angara .abs Build-System Tests${RESET}\n"
printf "${CYAN}  Compiler: $ANGC${RESET}\n"
printf "${CYAN}════════════════════════════════════════════════════════${RESET}\n\n"

if [ ! -f "$ANGC" ]; then
    printf "${RED}Error: Compiler not found at $ANGC${RESET}\n"
    printf "${YELLOW}Run 'make' first to build the compiler.${RESET}\n"
    exit 1
fi

# Each test is a directory under tests/build_system/ containing project.abs + src/.
for test_dir in "$SCRIPT_DIR/"*/; do
    [ -d "$test_dir" ] || continue
    [ -f "$test_dir/project.abs" ] || continue
    test_name="$(basename "$test_dir")"

    entry="$test_dir/src/main.an"
    [ -f "$entry" ] || entry="$test_dir/main.an"

    # Read markers from the entry file.
    expect_exit=$(strip_ansi < "$entry" | /bin/grep -oE "exit: -?[0-9]+" | head -1 | awk '{print $2}' || true)
    [ -z "$expect_exit" ] && expect_exit=0
    mapfile -t expect_stdout < <(strip_ansi < "$entry" | /bin/grep -oP 'stdout:\s*\K.*' || true)

    printf "  ${BOLD}${test_name}${RESET} ${DIM}(exit $expect_exit)${RESET}: "

    # Build in an isolated copy so the test is hermetic and doesn't pollute the
    # repo's own .angara cache.
    work="$(mktemp -d)"
    cp -r "$test_dir/." "$work/"
    binary="$work/app"

    build_output=$("$ANGC" --path "$work/project.abs" 2>&1) && build_rc=$? || build_rc=$?

    # Bug 2 regression check: the build must not fail with "cannot find -l<name>".
    if [ $build_rc -ne 0 ]; then
        if echo "$build_output" | strip_ansi | grep -qi "cannot find -l"; then
            printf "${RED}BUG2 FAIL${RESET} (spurious -l flag)\n"
            BUGS+=("BUG [$test_name]: Bug 2 regressed — linker cannot find -l<module>")
        elif echo "$build_output" | strip_ansi | grep -qi "multiple definition"; then
            printf "${RED}BUG1 FAIL${RESET} (duplicate symbols)\n"
            BUGS+=("BUG [$test_name]: Bug 1 regressed — multiple definition of wrappers")
        else
            printf "${RED}BUILD FAIL${RESET}\n"
            BUGS+=("BUG [$test_name]: .abs build failed unexpectedly")
        fi
        echo "$build_output" | strip_ansi | grep -i "error" | head -3 | sed 's/^/         /'
        FAIL=$((FAIL + 1))
        rm -rf "$work"
        continue
    fi

    # Locate the produced binary (BuildSystem emits <output_dir>/<project_name>).
    actual_binary="$binary"
    [ -f "$actual_binary" ] || actual_binary="$work/$(basename "$test_dir")"
    [ -f "$actual_binary" ] || actual_binary="$(find "$work" -type f -perm -u+x | head -1)"

    if [ -z "$actual_binary" ] || [ ! -f "$actual_binary" ]; then
        printf "${RED}NO BINARY${RESET}\n"
        BUGS+=("BUG [$test_name]: build succeeded but no binary emitted")
        FAIL=$((FAIL + 1))
        rm -rf "$work"
        continue
    fi

    run_output=$("$actual_binary" 2>&1) && run_rc=$? || run_rc=$?

    if [ "$run_rc" != "$expect_exit" ]; then
        printf "${RED}WRONG EXIT${RESET} (got $run_rc, expected $expect_exit)\n"
        BUGS+=("BUG [$test_name]: exit $run_rc, expected $expect_exit")
        FAIL=$((FAIL + 1))
        rm -rf "$work"
        continue
    fi

    stdout_fail=0
    for pattern in "${expect_stdout[@]}"; do
        if ! echo "$run_output" | grep -qF -- "$pattern"; then
            printf "${RED}STDOUT MISMATCH${RESET} (missing: '%s')\n" "$pattern"
            BUGS+=("BUG [$test_name]: expected stdout '$pattern'")
            stdout_fail=1
            break
        fi
    done
    if [ $stdout_fail -eq 1 ]; then
        FAIL=$((FAIL + 1))
        rm -rf "$work"
        continue
    fi

    printf "${GREEN}PASS${RESET}\n"
    PASS=$((PASS + 1))
    rm -rf "$work"
done

printf "\n"
if [ ${#BUGS[@]} -gt 0 ]; then
    printf "${BOLD}${RED}── Bug Report ──${RESET}\n\n"
    for i in "${!BUGS[@]}"; do
        printf "  ${BOLD}${RED} %d. %s${RESET}\n" "$((i+1))" "${BUGS[$i]}"
    done
    printf "\n"
fi

printf "${CYAN}════════════════════════════════════════════════════════${RESET}\n"
printf "  ${BOLD}Results:${RESET} ${GREEN}${PASS} passed${RESET}, ${RED}${FAIL} failed${RESET} out of $((PASS+FAIL)) .abs tests\n"
printf "${CYAN}════════════════════════════════════════════════════════${RESET}\n\n"

[ $FAIL -eq 0 ]
