// C benchmark: Integer matrix multiplication (equivalent to bench_matmul.an)
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    long n = 200;
    long total = n * n;

    long *a = malloc(total * sizeof(long));
    long *b = malloc(total * sizeof(long));
    long *c = malloc(total * sizeof(long));

    for (long i = 0; i < total; i++) {
        a[i] = i % 10;
        b[i] = (i + 3) % 10;
        c[i] = 0;
    }

    for (long i = 0; i < n; i++) {
        for (long j = 0; j < n; j++) {
            long sum = 0;
            for (long k = 0; k < n; k++) {
                sum += a[i * n + k] * b[k * n + j];
            }
            c[i * n + j] = sum;
        }
    }

    long result = 0;
    for (long i = 0; i < total; i++) {
        result += c[i];
    }

    printf("Matrix multiply %ldx%ld sum: %ld\n", n, n, result);

    free(a);
    free(b);
    free(c);
    return 0;
}
