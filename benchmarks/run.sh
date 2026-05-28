#!/bin/bash
# Angara vs C — Performance Benchmark Suite
# Compiles and runs each benchmark multiple times, averages the results.

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

# ── Compile everything ──────────────────────────────────────────────────────

echo -e "${BOLD}╔══════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BOLD}║        Angara vs C — Performance Benchmark Suite            ║${NC}"
echo -e "${BOLD}╚══════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "${CYAN}Compiling benchmarks...${NC}"

# Compile Angara (release/O2) and C (-O2)
for bench in "${BENCHMARKS[@]}"; do
    echo -e "  ${YELLOW}Angara (release)${NC}: $bench"
    "$ANGC" --release "$BENCH_DIR/${bench}.an" -o "$BUILD_DIR/${bench}_an" 2>/dev/null || {
        echo -e "  ${RED}FAILED to compile $bench.an${NC}"
        exit 1
    }
done

for bench in "${BENCHMARKS[@]}"; do
    echo -e "  ${YELLOW}C (-O2)${NC}:          $bench"
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

printf "${BOLD}%-30s │ %-12s │ %-12s │ %-10s${NC}\n" "Benchmark" "Angara (s)" "C -O2 (s)" "Ratio"
printf "%-30s─┼─%-12s─┼─%-12s─┼─%-10s\n" "──────────────────────────────" "────────────" "────────────" "──────────"

for bench in "${BENCHMARKS[@]}"; do
    an_bin="$BUILD_DIR/${bench}_an"
    c_bin="$BUILD_DIR/${bench}_c"

    # Run Angara benchmark
    an_total=0
    for ((run=1; run<=RUNS; run++)); do
        t=$(run_once "$an_bin")
        an_total=$(echo "$an_total + $t" | bc -l)
    done
    an_avg=$(echo "scale=4; $an_total / $RUNS" | bc -l)

    # Run C benchmark
    c_total=0
    for ((run=1; run<=RUNS; run++)); do
        t=$(run_once "$c_bin")
        c_total=$(echo "$c_total + $t" | bc -l)
    done
    c_avg=$(echo "scale=4; $c_total / $RUNS" | bc -l)

    # Ratio (Angara / C)
    ratio=$(echo "scale=2; $an_avg / $c_avg" | bc -l)

    # Color the ratio
    ratio_int=$(echo "$ratio / 1" | bc)
    if (( ratio_int <= 1 )); then
        ratio_color=$GREEN
    elif (( ratio_int <= 2 )); then
        ratio_color=$YELLOW
    else
        ratio_color=$RED
    fi

    printf "%-30s │ %10.4fs  │ %10.4fs  │ ${ratio_color}%8.2fx${NC}\n" \
        "${DESC[$bench]}" "$an_avg" "$c_avg" "$ratio"
done

echo ""
echo -e "${CYAN}Notes:${NC}"
echo -e "  • Each benchmark was run ${RUNS} times; times are averages."
echo -e "  • ${GREEN}Ratio <= 1x${NC} = Angara matches/beats C"
echo -e "  • ${YELLOW}Ratio 1-2x${NC}  = small overhead"
echo -e "  • ${RED}Ratio > 2x${NC}  = significant overhead (worth investigating)"
echo -e "  • Angara was compiled with --release (O2), C with clang -O2"
echo -e "  • Results are wall-clock time on $(uname -s) $(uname -m)"
echo ""
