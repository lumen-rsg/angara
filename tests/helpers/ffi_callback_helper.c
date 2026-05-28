// Simple C helper for testing userdata-based callbacks from Angara FFI.
// Provides a function that takes a callback + userdata and invokes it.

#include <stdint.h>

// Apply a function to each element of an integer array using a callback with userdata.
// Signature: void apply_ints(int64_t* arr, int count, void (*fn)(int64_t, void*), void* userdata)
void apply_ints(int64_t* arr, int count, void (*fn)(int64_t, void*), void* userdata) {
    for (int i = 0; i < count; i++) {
        fn(arr[i], userdata);
    }
}

// Sum integers via callback with userdata (for testing return values).
// Signature: int64_t reduce_sum(int64_t* arr, int count, int64_t (*fn)(int64_t, void*), void* userdata)
int64_t reduce_sum(int64_t* arr, int count, int64_t (*fn)(int64_t, void*), void* userdata) {
    int64_t sum = 0;
    for (int i = 0; i < count; i++) {
        sum += fn(arr[i], userdata);
    }
    return sum;
}
