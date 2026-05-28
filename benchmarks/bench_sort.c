// C benchmark: Bubble sort (equivalent to bench_sort.an)
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

int main(void) {
    long n = 20000;

    // Build reverse-sorted array
    long *arr = malloc(n * sizeof(long));
    for (long i = 0; i < n; i++) {
        arr[i] = n - 1 - i;
    }

    // Bubble sort
    bool swapped = true;
    while (swapped) {
        swapped = false;
        for (long i = 1; i < n; i++) {
            if (arr[i - 1] > arr[i]) {
                long tmp = arr[i - 1];
                arr[i - 1] = arr[i];
                arr[i] = tmp;
                swapped = true;
            }
        }
    }

    // Verify sorted
    bool sorted = true;
    for (long i = 1; i < n; i++) {
        if (arr[i - 1] > arr[i]) sorted = false;
    }

    printf("Sorted: %s\n", sorted ? "true" : "false");
    printf("First: %ld Last: %ld\n", arr[0], arr[n - 1]);

    free(arr);
    return 0;
}
