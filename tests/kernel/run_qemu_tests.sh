#!/bin/bash
# ═══════════════════════════════════════════════════════════════
# Angara QEMU Boot Test Suite
#
# Boots the bare-metal examples in QEMU and asserts the expected serial
# output appears — catching codegen → link → boot regressions that the
# compile-contract and IR-lowering suites can't detect.
#
# Invokes each example's own `make <verify-target>` (which encapsulates
# the full build → boot → grep cycle). Skips gracefully if QEMU or the
# cross-toolchain is not installed.
#
# Usage: ./run_qemu_tests.sh
# ═══════════════════════════════════════════════════════════════

set -uo pipefail

RED='\033[1;31m'; GREEN='\033[1;32m'; CYAN='\033[1;36m'; DIM='\033[2'; BOLD='\033[1m'; YELLOW='\033[1;33m'; RESET='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

PASS=0; FAIL=0; BUGS=()

# ── Toolchain availability ──────────────────────────────────────
# All four tools are required. If any are missing, SKIP (exit 0) rather
# than FAIL — QEMU and the cross-toolchain may not be on every CI host.
MISSING=()
for tool in qemu-system-aarch64 clang ld.bfd llvm-objcopy; do
    command -v "$tool" >/dev/null 2>&1 || MISSING+=("$tool")
done
if [ ${#MISSING[@]} -gt 0 ]; then
    printf "${YELLOW}SKIP${RESET} QEMU boot tests — missing: %s\n" "${MISSING[*]}"
    printf "${DIM}  Install qemu-system-aarch64 + a clang/ld.bfd/llvm-objcopy toolchain to enable.${RESET}\n"
    exit 0
fi

echo ""
printf "${CYAN}════════════════════════════════════════════════════════${RESET}\n"
printf "${CYAN}  Angara QEMU Boot Test Suite${RESET}\n"
printf "${CYAN}════════════════════════════════════════════════════════${RESET}\n\n"

# ── Helper: run one example's verify target ─────────────────────
# Args: dir_name  verify_target  description
run_qemu_example() {
    local dir="$1" target="$2" desc="$3"
    local full_dir="$PROJECT_DIR/examples/$dir"

    printf "  ${BOLD}%s${RESET} ${DIM}(%s)${RESET}: " "$dir" "$desc"

    if [ ! -d "$full_dir" ]; then
        printf "${RED}FAIL${RESET} (directory not found)\n"
        FAIL=$((FAIL + 1)); BUGS+=("$dir: missing directory")
        return
    fi

    # Clean build artifacts first, then run the verify target. The Makefile's
    # verify target handles build → timeout qemu → grep internally.
    local output
    output=$(cd "$full_dir" && make clean > /dev/null 2>&1 && make "$target" 2>&1) || true

    # The verify targets print [OK] on success, [FAIL] on failure.
    if echo "$output" | grep -q "\[OK\]"; then
        printf "${GREEN}PASS${RESET}\n"
        # Show the key assertion lines.
        echo "$output" | grep "\[OK\]" | head -5 | sed 's/^/         /'
        PASS=$((PASS + 1))
    else
        printf "${RED}FAIL${RESET}\n"
        echo "$output" | grep -iE "fail|error|missing" | head -3 | sed 's/^/         /'
        FAIL=$((FAIL + 1)); BUGS+=("$dir: verify-$target failed")
    fi
}

# ── Run the examples ────────────────────────────────────────────
# qemu_virt: verify-intrinsics asserts LOCK/CPU/CYC/FRQ/CLZ/RBIT lines.
# Exercises: atomics, CPU register reads, bit ops — all on the real CPU.
run_qemu_example "qemu_virt" "verify-intrinsics" \
    "atomics + CPU regs + bit ops boot"

# qemu_virt_irq: verify-irq asserts boot banner + TICK= lines.
# Exercises: GIC init, timer, exception vector, IRQ handler.
run_qemu_example "qemu_virt_irq" "verify-irq" \
    "GIC + timer interrupt boot"

echo ""
printf "${BOLD}QEMU boot results: ${GREEN}%d passed${RESET}, ${RED}%d failed${RESET}\n" "$PASS" "$FAIL"
if [ ${#BUGS[@]} -gt 0 ]; then
    printf "${RED}Failures:${RESET}\n"
    for b in "${BUGS[@]}"; do printf "  - %s\n" "$b"; done
fi

[ "$FAIL" -eq 0 ]
