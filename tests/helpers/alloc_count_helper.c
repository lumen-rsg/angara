// ============================================================================
// alloc_count_helper.c — counting allocator for the hosted swap-allocator test.
//
// Mirrors the %Allocator vtable shape from angc/backend/llvm/rt/Memory.cpp:
//   { ptr alloc(i64), ptr realloc(ptr,i64,i64), ptr free(ptr,i64) }
//
// The Angara program (tests/kernel/alloc_swap.an) calls
// angara_install_counting_allocator() via foreign func; this calls the module's
// external __ang_allocator_init_main(&vtable) to swap the runtime's allocator.
// After list/record/string growth, the program reads angara_alloc_count() and
// asserts it incremented — proving the swap routes traffic through the vtable.
// ============================================================================

#include <stdlib.h>
#include <stdio.h>

// The module-qualified init symbol name depends on the .an source filename
// (the module name). Default to "main"; override at compile time with
// -DANG_ALLOCATOR_MODULE=modname if the test module is named differently.
#ifndef ANG_ALLOCATOR_MODULE
#define ANG_ALLOCATOR_MODULE main
#endif
#define ANG_PASTE(a, b) a ## _ ## b
#define ANG_INIT_SYM(mod) ANG_PASTE(__ang_allocator_init, mod)

typedef void* (*alloc_fn)(unsigned long);
typedef void* (*realloc_fn)(void*, unsigned long, unsigned long);
typedef void  (*free_fn)(void*, unsigned long);

struct AngaraAllocator { alloc_fn alloc; realloc_fn realloc; free_fn free; };

// Declared by the Angara-compiled module (ExternalLinkage, module-qualified).
extern void ANG_INIT_SYM(ANG_ALLOCATOR_MODULE)(void* allocator);

static long g_alloc_count = 0;
static long g_free_count = 0;

static void* count_alloc(unsigned long size) {
    g_alloc_count++;
    return malloc(size);
}
static void* count_realloc(void* p, unsigned long old_size, unsigned long new_size) {
    (void)old_size;
    g_alloc_count++;
    return realloc(p, new_size);
}
static void count_free(void* p, unsigned long size) {
    (void)size;
    if (p) g_free_count++;
    free(p);
}

static struct AngaraAllocator counting_allocator = {
    count_alloc, count_realloc, count_free
};

// foreign func angara_install_counting_allocator() -> i64
long angara_install_counting_allocator(void) {
    ANG_INIT_SYM(ANG_ALLOCATOR_MODULE)(&counting_allocator);
    return 0;
}

// foreign func angara_alloc_count() -> i64
long angara_alloc_count(void) {
    return g_alloc_count;
}

// foreign func angara_free_count() -> i64
long angara_free_count(void) {
    return g_free_count;
}

// foreign func angara_reset_count() -> i64
long angara_reset_count(void) {
    g_alloc_count = 0;
    g_free_count = 0;
    return 0;
}
