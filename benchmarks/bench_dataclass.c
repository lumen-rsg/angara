// C benchmark: Struct allocation (equivalent to bench_dataclass.an)
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    long x;
    long y;
    long z;
} Point;

static long distance_sq(Point p) {
    return p.x * p.x + p.y * p.y + p.z * p.z;
}

int main(void) {
    long n = 500000;
    long sum = 0;

    for (long i = 0; i < n; i++) {
        Point p = {i, i * 2, i * 3};
        sum += distance_sq(p);
    }

    printf("Sum of distances: %ld\n", sum);
    return 0;
}
