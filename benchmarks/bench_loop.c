// C benchmark: Tight integer loop (equivalent to bench_loop.an)
#include <stdio.h>

int main(void) {
    long n = 500000000;
    long sum = 0;
    for (long i = 0; i < n; i++) {
        sum += 1;
    }
    printf("Sum: %ld\n", sum);
    return 0;
}
