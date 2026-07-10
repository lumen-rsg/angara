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
    # Skip files with no expect: marker — they belong to a sibling runner
    # (e.g. fs_ir_probe.an is compiled by run_ir_tests.sh via --emit-llvm).
    if [ -z "$expected" ]; then
        continue
    fi
    # mode: --kernel (default), --freestanding, or hosted. Hosted tests are
    # run by a separate runner (they link a C helper), so skip them here.
    mode=$(strip_ansi < "$test_file" | /bin/grep -oE "mode: (hosted|--[a-z]+)" | head -1 | awk '{print $2}')
    if [ "$mode" = "hosted" ]; then
        continue   # owned by run_alloc_swap_test.sh
    fi
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

# IR-lowering assertions: emit each freestanding intrinsic's LLVM IR and assert
# the expected lowering is present. Runs only if the gate suite passed, so a
# single failing gate isn't masked by an IR crash.
IR_RC=0
if [ "$FAIL" -eq 0 ]; then
    bash "$SCRIPT_DIR/run_ir_tests.sh" "$ANGC" || IR_RC=$?
    if [ "$IR_RC" -ne 0 ]; then
        FAIL=$((FAIL + 1)); BUGS+=("IR-lowering suite: see output above")
    fi
fi

[ "$FAIL" -eq 0 ]
