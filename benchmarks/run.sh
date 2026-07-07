#!/bin/bash
# Angara vs C — Performance Benchmark Suite
# Compiles and runs each benchmark multiple times, averages the results.
# Tests: C (-O2), Angara/Chaperone (--release)

set -euo pipefail

BENCH_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$BENCH_DIR/.." && pwd)"
ANGC="$ROOT_DIR/build/angc"
BUILD_DIR="$BENCH_DIR/build"
RUNS=5

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
DIM='\033[2m'
BOLD='\033[1m'
NC='\033[0m'

mkdir -p "$BUILD_DIR"

# Benchmarks to run (name only, no extension)
BENCHMARKS=(
    "bench_loop"
    "bench_primes"
    "bench_fib_recursive"
    "bench_matmul"
    "bench_lists"
    "bench_sort"
    "bench_strings"
    "bench_dataclass"
    "bench_mem_stress"
)

# Friendly descriptions
declare -A DESC
DESC[bench_loop]="Integer Loop (500M iter)"
DESC[bench_primes]="Prime Sieve (1M)"
DESC[bench_fib_recursive]="Recursive Fibonacci (n=40)"
DESC[bench_matmul]="Matrix Multiply (200x200)"
DESC[bench_lists]="List Ops (1M items)"
DESC[bench_sort]="Bubble Sort (20K)"
DESC[bench_strings]="String Building (100K)"
DESC[bench_dataclass]="Data Class Churn (500K)"
DESC[bench_mem_stress]="Memory Stress (trees+strings+lists)"

# ── Compile everything ──────────────────────────────────────────────────────

echo -e "${BOLD}╔════════════════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BOLD}║           Angara vs C — Performance Benchmark Suite                      ║${NC}"
echo -e "${BOLD}╚════════════════════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "${CYAN}Compiling benchmarks...${NC}"

# Compile Angara/Chaperone (default allocator)
for bench in "${BENCHMARKS[@]}"; do
    echo -e "  ${YELLOW}Angara${NC}: $bench"
    "$ANGC" --release "$BENCH_DIR/${bench}.an" -o "$BUILD_DIR/${bench}_angara" 2>/dev/null || {
        echo -e "  ${RED}FAILED to compile $bench.an${NC}"
        exit 1
    }
done

# Compile C (-O2)
for bench in "${BENCHMARKS[@]}"; do
    echo -e "  ${YELLOW}C (-O2)${NC}:           $bench"
    clang -O2 "$BENCH_DIR/${bench}.c" -o "$BUILD_DIR/${bench}_c" -lm 2>/dev/null || {
        echo -e "  ${RED}FAILED to compile $bench.c${NC}"
        exit 1
    }
done

echo ""
echo -e "${GREEN}All benchmarks compiled successfully.${NC}"
echo ""

# ── Timing function ─────────────────────────────────────────────────────────

run_once() {
    local bin="$1"
    local tmpfile
    tmpfile=$(mktemp)
    /usr/bin/time -p "$bin" > /dev/null 2>"$tmpfile"
    grep "^real" "$tmpfile" | awk '{print $2}'
    rm -f "$tmpfile"
}

# ── Table output ────────────────────────────────────────────────────────────

# Header
printf "${BOLD}%-30s │ %-11s │ %-11s │ %-9s${NC}\n" \
    "Benchmark" "Angara (s)" "C -O2 (s)" "Ratio"
printf "%-30s─┼─%-11s─┼─%-11s─┼─%-9s\n" \
    "──────────────────────────────" "───────────" "───────────" "─────────"

for bench in "${BENCHMARKS[@]}"; do
    angara_bin="$BUILD_DIR/${bench}_angara"
    c_bin="$BUILD_DIR/${bench}_c"

    # Run Angara benchmark
    angara_total=0
    for ((run=1; run<=RUNS; run++)); do
        t=$(run_once "$angara_bin")
        angara_total=$(echo "$angara_total + $t" | bc -l)
    done
    angara_avg=$(echo "scale=4; $angara_total / $RUNS" | bc -l)

    # Run C benchmark
    c_total=0
    for ((run=1; run<=RUNS; run++)); do
        t=$(run_once "$c_bin")
        c_total=$(echo "$c_total + $t" | bc -l)
    done
    c_avg=$(echo "scale=4; $c_total / $RUNS" | bc -l)

    # Ratio
    angara_ratio=$(echo "scale=2; $angara_avg / $c_avg" | bc -l)

    # Color helper
    color_for_ratio() {
        local r=$1
        local r_int=$(echo "$r / 1" | bc)
        if (( r_int <= 1 )); then
            echo "$GREEN"
        elif (( r_int <= 2 )); then
            echo "$YELLOW"
        else
            echo "$RED"
        fi
    }

    angara_color=$(color_for_ratio "$angara_ratio")

    printf "%-30s │ %9.4fs  │ %9.4fs  │ ${angara_color}%7.2fx${NC}\n" \
        "${DESC[$bench]}" "$angara_avg" "$c_avg" "$angara_ratio"
done

echo ""
echo -e "${CYAN}Notes:${NC}"
echo -e "  • Each benchmark was run ${RUNS} times; times are averages."
echo -e "  • ${GREEN}Ratio <= 1x${NC} = matches/beats C"
echo -e "  • ${YELLOW}Ratio 1-2x${NC}  = small overhead"
echo -e "  • ${RED}Ratio > 2x${NC}  = significant overhead (worth investigating)"
echo -e "  • All Angara builds use --release (O2), C uses clang -O2"
echo -e "  • Results are wall-clock time on $(uname -s) $(uname -m)"
echo ""
