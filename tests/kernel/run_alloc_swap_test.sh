#!/bin/bash
# ═══════════════════════════════════════════════════════════════
# Allocator-swap hosted test — proves __ang_allocator_init_<mod>
# routes heap traffic through a swapped vtable.
#
# Compiles a counting allocator (tests/helpers/alloc_count_helper.c),
# links it into tests/kernel/alloc_swap.an, runs it, and asserts the
# counter incremented (i.e. the swap was honored across list growth,
# record construction, and string concat).
#
# Usage: ./run_alloc_swap_test.sh [compiler_path] [clang_path]
# ═══════════════════════════════════════════════════════════════

set -uo pipefail

RED='\033[1;31m'; GREEN='\033[1;32m'; CYAN='\033[1;36m'; DIM='\033[2m'; BOLD='\033[1m'; RESET='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
ANGC="${1:-$PROJECT_DIR/build/angc}"
CLANG="${2:-clang}"

TMPDIR_TEST="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_TEST"' EXIT

# Sanitize PATH (a ZCode AppImage mount can shadow make/clang).
CLEAN_PATH=$(echo "$PATH" | tr ':' '\n' | /usr/bin/grep -v "mount_ZCode\|\.mount_" | paste -sd:)
export PATH="$CLEAN_PATH"

printf "${CYAN}════════════════════════════════════════════════════════${RESET}\n"
printf "${CYAN}  Allocator-Swap Test (hosted)${RESET}\n"
printf "${CYAN}  Compiler: $ANGC${RESET}\n"
printf "${CYAN}════════════════════════════════════════════════════════${RESET}\n\n"

# 1. Compile the counting-allocator helper (module name = alloc_swap).
helper_obj="$TMPDIR_TEST/alloc_count_helper.o"
"$CLANG" -c -fPIC -DANG_ALLOCATOR_MODULE=alloc_swap \
    "$PROJECT_DIR/tests/helpers/alloc_count_helper.c" -o "$helper_obj" 2>&1
if [ ! -f "$helper_obj" ]; then
    printf "${RED}FAIL${RESET} (helper did not compile)\n"; exit 1
fi
printf "${DIM}  helper compiled${RESET}\n"

# 2. Compile + link the .an with the helper. The module name derives from the
#    source filename (alloc_swap.an → module "alloc_swap"), so build from a
#    same-filesystem temp dir that preserves the basename. The helper was
#    compiled with -DANG_ALLOCATOR_MODULE=alloc_swap to match.
src="$SCRIPT_DIR/alloc_swap.an"
work="$PROJECT_DIR/.alloc_swap_work"
mkdir -p "$work"
cp "$src" "$work/alloc_swap.an"
"$ANGC" --force "$work/alloc_swap.an" -l "$helper_obj" -o "$work/alloc_swap" >/tmp/alloc_swap.log 2>&1
linked=$?
if [ $linked -ne 0 ] || [ ! -f "$work/alloc_swap" ]; then
    printf "${RED}FAIL${RESET} (compile/link failed)\n"
    /bin/grep -iE "error|undefined" /tmp/alloc_swap.log | head -4 | sed 's/^/         /'
    rm -rf "$work"; exit 1
fi
printf "${DIM}  linked${RESET}\n"

# 3. Run: exit 0 = swap routed allocations; non-zero = failure.
"$work/alloc_swap" >/tmp/alloc_swap_run.log 2>&1
rc=$?
/bin/cat /tmp/alloc_swap_run.log | sed 's/^/  /'
rm -rf "$work"

if [ $rc -eq 0 ]; then
    printf "${GREEN}PASS${RESET} (allocator swap routed allocations through the vtable)\n"
    exit 0
else
    printf "${RED}FAIL${RESET} (exit $rc — swap did not route allocations)\n"
    exit 1
fi
