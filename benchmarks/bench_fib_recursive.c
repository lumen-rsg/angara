// C benchmark: Recursive Fibonacci (equivalent to bench_fib_recursive.an)
#include <stdio.h>

static long fib(long n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

int main(void) {
    long n = 40;
    long result = fib(n);
    printf("fib(%ld) = %ld\n", n, result);
    return 0;
}
