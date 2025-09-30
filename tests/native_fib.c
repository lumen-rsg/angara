//
// Created by cv2 on 10/1/25.
//
#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include <stdlib.h>

// A pure C implementation of recursive Fibonacci.
// Using 64-bit integers to match Angara's i64.
uint64_t fib(uint64_t n) {
    if (n <= 1) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

int main(int argc, char* argv[]) {
    // We'll calculate up to Fib(40) as a reasonable benchmark.
    const uint64_t N = 40;

    // --- Time Measurement ---
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // --- The Core Work ---
    uint64_t result = fib(N);

    // --- End Time Measurement ---
    clock_gettime(CLOCK_MONOTONIC, &end);
    double time_taken = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("C Native Benchmark\n");
    printf("---------------------------\n");
    printf("Fibonacci(%llu) = %llu\n", N, result);
    printf("Time taken: %.4f seconds\n", time_taken);

    return 0;
}