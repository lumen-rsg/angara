// C benchmark: Prime counting (equivalent to bench_primes.an)
#include <stdio.h>
#include <stdbool.h>

static bool is_prime(long n) {
    if (n < 2) return false;
    if (n < 4) return true;
    if (n % 2 == 0) return false;
    if (n < 9) return true;
    if (n % 3 == 0) return false;
    for (long i = 5; i * i <= n; ) {
        if (n % i == 0) return false;
        if (n % (i + 2) == 0) return false;
        i += 6;
    }
    return true;
}

static long count_primes(long limit) {
    long count = 0;
    for (long i = 2; i < limit; i++) {
        if (is_prime(i)) count++;
    }
    return count;
}

int main(void) {
    long limit = 1000000;
    long result = count_primes(limit);
    printf("Primes below %ld: %ld\n", limit, result);
    return 0;
}
