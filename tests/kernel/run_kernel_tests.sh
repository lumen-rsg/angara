#!/bin/bash
# ═══════════════════════════════════════════════════════════════
# Angara Kernel-Mode Test Suite Runner
#
# Verifies the --kernel gates (try/throw/spawn/Mutex → E900-E903) and that
# valid --kernel code compiles to a relocatable object with no entry point.
#
# Each test .an carries markers:
#   // mode: --kernel          always set; the compiler is invoked with this flag
#   // expect: E9xx             expected diagnostic code (gate tests)
#   // expect: PASS             positive test: must compile + emit a clean .o
#
# Usage: ./run_kernel_tests.sh [compiler_path]
# ═══════════════════════════════════════════════════════════════

set -uo pipefail

RED='\033[1;31m'; GREEN='\033[1;32m'; CYAN='\033[1;36m'; DIM='\033[2'; BOLD='\033[1m'; RESET='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
ANGC="${1:-$PROJECT_DIR/build/angc}"
TMPDIR_TEST="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_TEST"' EXIT

strip_ansi() { sed 's/\x1b\[[0-9;]*m//g'; }

PASS=0; FAIL=0; BUGS=()

echo ""
printf "${CYAN}════════════════════════════════════════════════════════${RESET}\n"
printf "${CYAN}  Angara Kernel-Mode Test Suite${RESET}\n"
printf "${CYAN}  Compiler: $ANGC${RESET}\n"
printf "${CYAN}════════════════════════════════════════════════════════${RESET}\n\n"

for test_file in "$SCRIPT_DIR/"*.an; do
    [ -f "$test_file" ] || continue
    test_name="$(basename "$test_file" .an)"

    expected=$(strip_ansi < "$test_file" | /bin/grep -oE "expect: [A-Za-z0-9]+" | head -1 | awk '{print $2}')
    # mode: --kernel (default) or --freestanding. Freestanding emits _start;
    # kernel mode emits no entry point.
    mode=$(strip_ansi < "$test_file" | /bin/grep -oE "mode: --[a-z]+" | head -1 | awk '{print $2}')
    [ -n "$mode" ] || mode="--kernel"
    printf "  ${BOLD}%s${RESET} ${DIM}(expect %s, %s)${RESET}: " "$test_name" "$expected" "$mode"

    out="$TMPDIR_TEST/${test_name}.o"
    compile_output=$("$ANGC" --force "$mode" "$test_file" -o "$out" 2>&1) || true
    clean=$(echo "$compile_output" | strip_ansi)

    if [ "$expected" = "PASS" ]; then
        # Positive: must emit a relocatable ELF.
        if [ ! -f "$out" ]; then
            printf "${RED}FAIL${RESET} (no object emitted)\n"
            echo "$clean" | /bin/grep -i error | head -2 | sed 's/^/         /'
            FAIL=$((FAIL + 1)); BUGS+=("$test_name: no object"); continue
        fi
        # Entry-point contract: --kernel must have NO _start/main; --freestanding
        # MUST have _start.
        if [ "$mode" = "--kernel" ]; then
            if nm "$out" 2>/dev/null | /bin/grep -qwE "_start|main"; then
                printf "${RED}FAIL${RESET} (kernel object contains _start or main)\n"
                FAIL=$((FAIL + 1)); BUGS+=("$test_name: entry point present"); continue
            fi
        else  # --freestanding
            if ! nm "$out" 2>/dev/null | /bin/grep -qw "_start"; then
                printf "${RED}FAIL${RESET} (freestanding object missing _start)\n"
                FAIL=$((FAIL + 1)); BUGS+=("$test_name: no _start"); continue
            fi
        fi
        printf "${GREEN}PASS${RESET} (clean relocatable object)\n"
        PASS=$((PASS + 1))
    else
        # Negative gate: must emit the expected E9xx and fail to compile.
        if echo "$clean" | /bin/grep -qE "\\b${expected}\\b"; then
            printf "${GREEN}CAUGHT${RESET} %s\n" "$(echo "$clean" | /bin/grep -E "\\b${expected}\\b" | head -1)"
            PASS=$((PASS + 1))
        else
            printf "${RED}FAIL${RESET} (expected %s not found)\n" "$expected"
            echo "$clean" | /bin/grep -iE "error" | head -2 | sed 's/^/         /'
            FAIL=$((FAIL + 1)); BUGS+=("$test_name: missing $expected")
        fi
    fi
done

echo ""
printf "${BOLD}Kernel gate results: ${GREEN}%d passed${RESET}, ${RED}%d failed${RESET}\n" "$PASS" "$FAIL"
if [ ${#BUGS[@]} -gt 0 ]; then
    printf "${RED}Failures:${RESET}\n"
    for b in "${BUGS[@]}"; do printf "  - %s\n" "$b"; done
fi
[ "$FAIL" -eq 0 ]
