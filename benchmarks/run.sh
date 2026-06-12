#!/bin/bash
# Angara vs C — Performance Benchmark Suite
# Compiles and runs each benchmark multiple times, averages the results.
# Tests: C (-O2), Angara/Chaperone (--release), Angara/MarkSweep (--release --gc mark-sweep)

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
    "bench_gc_stress"
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
DESC[bench_gc_stress]="GC Stress (trees+strings+lists)"

# ── Compile everything ──────────────────────────────────────────────────────

echo -e "${BOLD}╔════════════════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BOLD}║           Angara vs C — Performance Benchmark Suite                      ║${NC}"
echo -e "${BOLD}╚════════════════════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "${CYAN}Compiling benchmarks...${NC}"

# Compile Angara/Chaperone (default GC)
for bench in "${BENCHMARKS[@]}"; do
    echo -e "  ${YELLOW}Angara/Chaperone${NC}: $bench"
    "$ANGC" --release "$BENCH_DIR/${bench}.an" -o "$BUILD_DIR/${bench}_chaperone" 2>/dev/null || {
        echo -e "  ${RED}FAILED to compile $bench.an (chaperone)${NC}"
        exit 1
    }
done

# Compile Angara/MarkSweep
for bench in "${BENCHMARKS[@]}"; do
    echo -e "  ${YELLOW}Angara/MarkSweep${NC}: $bench"
    "$ANGC" --release --gc mark-sweep "$BENCH_DIR/${bench}.an" -o "$BUILD_DIR/${bench}_ms" 2>/dev/null || {
        echo -e "  ${RED}FAILED to compile $bench.an (mark-sweep)${NC}"
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
printf "${BOLD}%-30s │ %-11s │ %-11s │ %-11s │ %-9s │ %-9s${NC}\n" \
    "Benchmark" "Chap. (s)" "MS (s)" "C -O2 (s)" "Chap/C" "MS/C"
printf "%-30s─┼─%-11s─┼─%-11s─┼─%-11s─┼─%-9s─┼─%-9s\n" \
    "──────────────────────────────" "───────────" "───────────" "───────────" "─────────" "─────────"

for bench in "${BENCHMARKS[@]}"; do
    chap_bin="$BUILD_DIR/${bench}_chaperone"
    ms_bin="$BUILD_DIR/${bench}_ms"
    c_bin="$BUILD_DIR/${bench}_c"

    # Run Chaperone benchmark
    chap_total=0
    for ((run=1; run<=RUNS; run++)); do
        t=$(run_once "$chap_bin")
        chap_total=$(echo "$chap_total + $t" | bc -l)
    done
    chap_avg=$(echo "scale=4; $chap_total / $RUNS" | bc -l)

    # Run MarkSweep benchmark
    ms_total=0
    for ((run=1; run<=RUNS; run++)); do
        t=$(run_once "$ms_bin")
        ms_total=$(echo "$ms_total + $t" | bc -l)
    done
    ms_avg=$(echo "scale=4; $ms_total / $RUNS" | bc -l)

    # Run C benchmark
    c_total=0
    for ((run=1; run<=RUNS; run++)); do
        t=$(run_once "$c_bin")
        c_total=$(echo "$c_total + $t" | bc -l)
    done
    c_avg=$(echo "scale=4; $c_total / $RUNS" | bc -l)

    # Ratios
    chap_ratio=$(echo "scale=2; $chap_avg / $c_avg" | bc -l)
    ms_ratio=$(echo "scale=2; $ms_avg / $c_avg" | bc -l)

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

    chap_color=$(color_for_ratio "$chap_ratio")
    ms_color=$(color_for_ratio "$ms_ratio")

    printf "%-30s │ %9.4fs  │ %9.4fs  │ %9.4fs  │ ${chap_color}%7.2fx${NC} │ ${ms_color}%7.2fx${NC}\n" \
        "${DESC[$bench]}" "$chap_avg" "$ms_avg" "$c_avg" "$chap_ratio" "$ms_ratio"
done

echo ""
echo -e "${CYAN}Notes:${NC}"
echo -e "  • Each benchmark was run ${RUNS} times; times are averages."
echo -e "  • ${GREEN}Ratio <= 1x${NC} = matches/beats C"
echo -e "  • ${YELLOW}Ratio 1-2x${NC}  = small overhead"
echo -e "  • ${RED}Ratio > 2x${NC}  = significant overhead (worth investigating)"
echo -e "  • ${DIM}Chap.${NC} = Chaperone GC (bump-arena), ${DIM}MS${NC} = Mark-Sweep GC"
echo -e "  • All Angara builds use --release (O2), C uses clang -O2"
echo -e "  • Results are wall-clock time on $(uname -s) $(uname -m)"
echo ""
