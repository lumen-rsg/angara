// C benchmark: GC stress equivalent — short-lived structs, string churn,
// short-lived arrays, map churn. Uses malloc/free (no GC overhead).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *key;
    long value;
} Pair;

static long make_pairs(long n) {
    long total = 0;
    for (long i = 0; i < n; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "key_%ld", i);
        Pair p;
        p.key = strdup(buf);
        p.value = i * i;
        total += p.value;
        free(p.key);
    }
    return total;
}

static long list_churn(long n) {
    long total = 0;
    for (long i = 0; i < n; i++) {
        long *arr = malloc(100 * sizeof(long));
        for (long j = 0; j < 100; j++) {
            arr[j] = i * 100 + j;
        }
        total += arr[0] + arr[49] + arr[99];
        free(arr);
    }
    return total;
}

static long string_churn(long n) {
    long len = 0;
    for (long i = 0; i < n; i++) {
        char buf[128];
        snprintf(buf, sizeof(buf), "prefix_%ld_suffix", i);
        len += (long)strlen(buf);
        char buf2[64];
        snprintf(buf2, sizeof(buf2), "%ld", i * i);
        len += (long)strlen(buf2);
    }
    return len;
}

static long record_churn(long n) {
    long total = 0;
    for (long i = 0; i < n; i++) {
        // Simulate record: 5 long values (like the Angara version)
        long vals[5] = {i, i * 2, i * 3, i * 4, i * 5};
        total += vals[0] + vals[1] + vals[3] + vals[4];
    }
    return total;
}

int main(void) {
    long pair_total = make_pairs(500000);
    long list_total = list_churn(5000);
    long str_total = string_churn(100000);
    long rec_total = record_churn(200000);

    printf("Pair total: %ld\n", pair_total);
    printf("List total: %ld\n", list_total);
    printf("String total: %ld\n", str_total);
    printf("Record total: %ld\n", rec_total);
    return 0;
}
