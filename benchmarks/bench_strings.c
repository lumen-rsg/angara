// C benchmark: String building (equivalent to bench_strings.an)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    long n = 100000;

    // Build string by repeated concatenation
    char *s = malloc(1);
    s[0] = '\0';
    long capa = 1;
    long slen = 0;

    for (long i = 0; i < n; i++) {
        if (slen + 1 >= capa) {
            capa = capa * 2;
            s = realloc(s, capa);
        }
        s[slen++] = 'x';
        s[slen] = '\0';
    }
    printf("String length: %ld\n", slen);
    free(s);

    // Convert 100000 integers to strings
    long count = 0;
    char buf[32];
    for (long i = 0; i < n; i++) {
        snprintf(buf, sizeof(buf), "%ld", i);
        count += strlen(buf);
    }
    printf("Total digits: %ld\n", count);

    return 0;
}
