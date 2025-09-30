//
// Created by cv2 on 10/1/25.
//

#include <stdio.h>
#include <time.h>
#include <stdint.h>

int main() {
    // A large number of iterations.
    const uint64_t ITERATIONS = 2000000000; // 2 billion
    uint64_t total = 0;

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // --- The Core Work: A tight loop with no function calls ---
    for (uint64_t i = 0; i < ITERATIONS; ++i) {
        total = total + 1;
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double time_taken = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("C Native Loop Benchmark\n");
    printf("---------------------------\n");
    printf("Total: %llu (to prevent optimization)\n", total);
    printf("Time taken: %.4f seconds\n", time_taken);

    return 0;
}