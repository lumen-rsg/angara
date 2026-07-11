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

printf "${BOLD}MMU control registers (inline asm)${RESET}\n"
assert "set_ttbr0 → msr ttbr0_el1"   "msr ttbr0_el1"
assert "set_mair → msr mair_el1"     "msr mair_el1"
assert "set_tcr → msr tcr_el1"       "msr tcr_el1"
assert "set_sctlr → msr sctlr_el1"   "msr sctlr_el1"
assert "get_sctlr → mrs sctlr_el1"   'mrs $0, sctlr_el1'
echo ""

printf "${BOLD}Tier 5: TLB management (inline asm)${RESET}\n"
assert "tlbi_vmalle1 → tlbi vmalle1"       "tlbi vmalle1"
assert "tlbi_vmalle1is → tlbi vmalle1is"   "tlbi vmalle1is"
assert "tlbi_alle1 → tlbi alle1"           "tlbi alle1"
assert "tlbi_alle1is → tlbi alle1is"       "tlbi alle1is"
assert "tlbi_vae1 → tlbi vae1"             "tlbi vae1"
assert "tlbi_vae1is → tlbi vae1is"         "tlbi vae1is"
assert "tlbi_vaae1 → tlbi vaae1"           "tlbi vaae1"
assert "tlbi_vaae1is → tlbi vaae1is"       "tlbi vaae1is"
assert "tlbi_aside1 → tlbi aside1"         "tlbi aside1"
assert "tlbi_aside1is → tlbi aside1is"     "tlbi aside1is"
assert "tlbi_vale1 → tlbi vale1"           "tlbi vale1"
assert "tlbi_vale1is → tlbi vale1is"       "tlbi vale1is"
echo ""

printf "${BOLD}Tier 5: privilege switching (inline asm)${RESET}\n"
assert "eret → eret"                "eret"
assert "set_spsr → msr spsr_el1"    "msr spsr_el1"
assert "set_elr → msr elr_el1"      "msr elr_el1"
echo ""

# ── F6: RISC-V Tier-1 intrinsic lowering ────────────────────────────────────
# Compile the RISC-V probe with a RISC-V target and grep for the expected
# inline-asm tokens. Uses a second IR file.
RV_IR_FILE="$(mktemp)"
# --emit-llvm prints IR to stdout; the -o path may still fail on cross-target
# CPU mismatches (e.g. host CPU invalid for RISC-V), so we check the IR file
# content rather than the exit code.
"$ANGC" --emit-llvm --freestanding --target riscv64-unknown-none-elf --cpu generic --force \
    "$SCRIPT_DIR/fs_ir_probe_rv.an" -o /tmp/fs_ir_probe_rv \
    2>/dev/null > "$RV_IR_FILE"
if [ ! -s "$RV_IR_FILE" ] || ! /bin/grep -qF "target triple" "$RV_IR_FILE"; then
    printf "${RED}FATAL${RESET}: --emit-llvm --target riscv64 failed to produce IR\n"
    rm -f "$RV_IR_FILE"
    FAIL=$((FAIL + 1)); BUGS+=("RISC-V IR: no output")
else
    rv_assert() {
        local label="$1" pattern="$2"
        if /bin/grep -qF -- "$pattern" "$RV_IR_FILE"; then
            printf "  ${GREEN}PASS${RESET}  ${DIM}%s${RESET} → %s\n" "$label" "$pattern"
            PASS=$((PASS + 1))
        else
            printf "  ${RED}FAIL${RESET}  ${BOLD}%s${RESET} — expected '%s' in RISC-V IR\n" "$label" "$pattern"
            FAIL=$((FAIL + 1)); BUGS+=("$label: missing '$pattern'")
        fi
    }
    printf "${BOLD}RISC-V Tier-1: counters & CSR & barriers (inline asm)${RESET}\n"
    rv_assert "rdcycle → rdcycle"          "rdcycle \$0"
    rv_assert "rdtime → rdtime"            "rdtime \$0"
    rv_assert "rdinstret → rdinstret"      "rdinstret \$0"
    rv_assert "csrr → csrrw"               "csrrw \$0, \$1, x0"
    rv_assert "csrw → csrrw x0"            "csrrw x0, \$0, \$1"
    rv_assert "fence → fence rw, rw"       "fence rw, rw"
    rv_assert "fence_i → fence.i"          "fence.i"
    rv_assert "sfence_vma → sfence.vma"    "sfence.vma zero, zero"
    rv_assert "wfi → wfi (RISC-V)"         "wfi"
    rv_assert "get_sp → mv sp (RISC-V)"    "mv \$0, sp"
    echo ""
fi
rm -f "$RV_IR_FILE"

printf "${BOLD}IR-lowering results: ${GREEN}%d passed${RESET}, ${RED}%d failed${RESET}\n\n" "$PASS" "$FAIL"
if [ ${#BUGS[@]} -gt 0 ]; then
    printf "${RED}Failures:${RESET}\n"
    for b in "${BUGS[@]}"; do printf "  - %s\n" "$b"; done
fi
[ "$FAIL" -eq 0 ]
