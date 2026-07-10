#!/bin/bash
# ═══════════════════════════════════════════════════════════════
# Angara Freestanding IR-Lowering Test Suite
#
# Compiles fs_ir_probe.an with --emit-llvm --freestanding and asserts the
# emitted textual IR contains the expected lowering for each intrinsic family.
# This catches lowering regressions (e.g. an atomic silently becoming a plain
# load) without requiring QEMU or a working cross-toolchain link step.
#
# Usage: ./run_ir_tests.sh [compiler_path]
# ═══════════════════════════════════════════════════════════════

set -uo pipefail

RED='\033[1;31m'; GREEN='\033[1;32m'; CYAN='\033[1;36m'; DIM='\033[2m'; BOLD='\033[1m'; RESET='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
ANGC="${1:-$PROJECT_DIR/build/angc}"

PASS=0; FAIL=0; BUGS=()

echo ""
printf "${CYAN}════════════════════════════════════════════════════════${RESET}\n"
printf "${CYAN}  Angara Freestanding IR-Lowering Suite${RESET}\n"
printf "${CYAN}  Compiler: $ANGC${RESET}\n"
printf "${CYAN}════════════════════════════════════════════════════════${RESET}\n\n"

# Emit the IR once. --emit-llvm prints to stdout and skips object generation,
# so this works without a cross-linker. Write to a file rather than a shell
# variable: command substitution can mangle long LLVM IR lines / trailing bytes.
IR_FILE="$(mktemp)"
trap 'rm -f "$IR_FILE"' EXIT
"$ANGC" --emit-llvm --freestanding --force "$SCRIPT_DIR/fs_ir_probe.an" -o /tmp/fs_ir_probe \
    2>/dev/null > "$IR_FILE"
if [ $? -ne 0 ] || [ ! -s "$IR_FILE" ]; then
    printf "${RED}FATAL${RESET}: --emit-llvm --freestanding failed to produce IR\n"
    exit 1
fi

# assert <label> <pattern>  — fixed-string match against the IR file.
# Uses /bin/grep to avoid any shell-function wrapper that mangles patterns.
assert() {
    local label="$1" pattern="$2"
    if /bin/grep -qF -- "$pattern" "$IR_FILE"; then
        printf "  ${GREEN}PASS${RESET}  ${DIM}%s${RESET} → %s\n" "$label" "$pattern"
        PASS=$((PASS + 1))
    else
        printf "  ${RED}FAIL${RESET}  ${BOLD}%s${RESET} — expected '%s' in IR\n" "$label" "$pattern"
        FAIL=$((FAIL + 1)); BUGS+=("$label: missing '$pattern'")
    fi
}

printf "${BOLD}Atomics (LLVM atomic IR)${RESET}\n"
assert "atomic_cas → cmpxchg"           "cmpxchg"
assert "atomic_add → atomicrmw add"     "atomicrmw add"
assert "atomic_load → load atomic"      "load atomic"
assert "atomic_store → store atomic"    "store atomic"
echo ""

printf "${BOLD}Bit manipulation (LLVM intrinsics)${RESET}\n"
assert "clz → llvm.ctlz"        "llvm.ctlz"
assert "rev → llvm.bswap"       "llvm.bswap"
assert "rbit → llvm.bitreverse" "llvm.bitreverse"
echo ""

printf "${BOLD}Control / termination${RESET}\n"
assert "halt → llvm.trap"   "llvm.trap"
echo ""

printf "${BOLD}CPU register reads (mrs inline asm)${RESET}\n"
assert "get_mpidr → mrs mpidr_el1"  'mrs $0, mpidr_el1'
assert "cntpct → mrs cntpct_el0"    'mrs $0, cntpct_el0'
assert "ttbr0_el1 → mrs ttbr0_el1"  'mrs $0, ttbr0_el1'
echo ""

printf "${BOLD}Barriers & DAIF (inline asm)${RESET}\n"
assert "dmb_st → dmb ishst"          "dmb ishst"
assert "get_daif → mrs daif"         'mrs $0, daif'
echo ""

printf "${BOLD}Cache maintenance (inline asm)${RESET}\n"
assert "dc_civac → dc civac"               "dc civac"
assert "dc_csw → csselr/ccsidr/cisw"       "csselr_el1"
assert "dc_csw → ccsidr read"              'mrs $0, ccsidr_el1'
assert "dc_csw → dc cisw"                  "dc cisw"
echo ""

printf "${BOLD}Context switching & vector table (inline asm)${RESET}\n"
assert "get_sp → mov sp"           'mov $0, sp'
assert "set_vbar → msr vbar_el1"   "msr vbar_el1"
echo ""

printf "${BOLD}Tier 5: TLB management (inline asm)${RESET}\n"
assert "tlbi_vmalle1 → tlbi vmalle1"   "tlbi vmalle1"
assert "tlbi_vaae1 → tlbi vaae1"       "tlbi vaae1"
echo ""

printf "${BOLD}Tier 5: privilege switching (inline asm)${RESET}\n"
assert "eret → eret"                "eret"
assert "set_spsr → msr spsr_el1"    "msr spsr_el1"
assert "set_elr → msr elr_el1"      "msr elr_el1"
echo ""

printf "${BOLD}IR-lowering results: ${GREEN}%d passed${RESET}, ${RED}%d failed${RESET}\n\n" "$PASS" "$FAIL"
if [ ${#BUGS[@]} -gt 0 ]; then
    printf "${RED}Failures:${RESET}\n"
    for b in "${BUGS[@]}"; do printf "  - %s\n" "$b"; done
fi
[ "$FAIL" -eq 0 ]
