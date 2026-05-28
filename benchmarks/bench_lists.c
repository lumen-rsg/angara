// C benchmark: Dynamic array operations (equivalent to bench_lists.an)
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    long size = 1000000;

    // Build dynamic array
    long *arr = malloc(size * sizeof(long));
    for (long i = 0; i < size; i++) {
        arr[i] = i;
    }

    // Sum
    long sum = 0;
    for (long i = 0; i < size; i++) {
        sum += arr[i];
    }

    // Count multiples of 7
    long count = 0;
    for (long i = 0; i < size; i++) {
        if (arr[i] % 7 == 0) count++;
    }

    printf("List size: %ld\n", size);
    printf("Sum: %ld\n", sum);
    printf("Multiples of 7: %ld\n", count);

    free(arr);
    return 0;
}
