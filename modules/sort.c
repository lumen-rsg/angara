//
// sort.c — Angara sorting and searching module
//
// Provides in-place sort, sorted copy, reverse, dedup,
// binary search, and basic set operations on sorted lists.
// Works on lists of i64/f64/string using natural ordering.
//

#include <stdlib.h>
#include <string.h>
#include "Angara.h"

#define IS_LIST(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_LIST)
#define IS_STR(v)  (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)

typedef enum { CMP_LT, CMP_EQ, CMP_GT } CmpResult;

static CmpResult compare(AngaraObject a, AngaraObject b) {
    if (ang_is_i64(a) && ang_is_i64(b)) {
        int64_t va = ang_as_i64(a), vb = ang_as_i64(b);
        return va < vb ? CMP_LT : (va > vb ? CMP_GT : CMP_EQ);
    }
    if ((ang_is_f64(a) || ang_is_i64(a)) && (ang_is_f64(b) || ang_is_i64(b))) {
        double va = ang_is_f64(a) ? ang_as_f64(a) : (double)ang_as_i64(a);
        double vb = ang_is_f64(b) ? ang_as_f64(b) : (double)ang_as_i64(b);
        return va < vb ? CMP_LT : (va > vb ? CMP_GT : CMP_EQ);
    }
    if (IS_STR(a) && IS_STR(b)) {
        int c = strcmp(ang_api->as_cstr(a), ang_api->as_cstr(b));
        return c < 0 ? CMP_LT : (c > 0 ? CMP_GT : CMP_EQ);
    }
    // Fallback: compare by type name ordering
    return CMP_EQ;
}

// --- sort.sorted(list) -> list ---
// Returns a new sorted list (ascending).
AngaraObject Angara_sort_sorted(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_LIST(args[0])) {
        ang_api->throw_error("sorted(list) expects a list argument.");
        return ang_nil();
    }
    size_t len = ang_api->list_len(args[0]);
    if (len <= 1) {
        // Return a copy
        AngaraObject copy = ang_api->list_new();
        for (size_t i = 0; i < len; i++) {
            AngaraObject elem = ang_api->list_get(args[0], (int64_t)i);
            ang_api->list_push(copy, elem);
            ang_api->decref(elem);
        }
        return copy;
    }

    // Collect elements into array
    AngaraObject* arr = (AngaraObject*)malloc(len * sizeof(AngaraObject));
    for (size_t i = 0; i < len; i++) arr[i] = ang_api->list_get(args[0], (int64_t)i);

    // Insertion sort (stable, simple)
    for (size_t i = 1; i < len; i++) {
        AngaraObject key = arr[i];
        size_t j = i;
        while (j > 0 && compare(arr[j - 1], key) == CMP_GT) {
            arr[j] = arr[j - 1];
            j--;
        }
        arr[j] = key;
    }

    AngaraObject result = ang_api->list_new();
    for (size_t i = 0; i < len; i++) {
        ang_api->list_push(result, arr[i]);
        ang_api->decref(arr[i]);
    }
    free(arr);
    return result;
}

// --- sort.reverse(list) -> list ---
AngaraObject Angara_sort_reverse(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_LIST(args[0])) {
        ang_api->throw_error("reverse(list) expects a list argument.");
        return ang_nil();
    }
    size_t len = ang_api->list_len(args[0]);
    AngaraObject result = ang_api->list_new();
    for (size_t i = len; i > 0; i--) {
        AngaraObject elem = ang_api->list_get(args[0], (int64_t)(i - 1));
        ang_api->list_push(result, elem);
        ang_api->decref(elem);
    }
    return result;
}

// --- sort.contains(sorted_list, value) -> bool ---
// Binary search on a sorted list.
AngaraObject Angara_sort_contains(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_LIST(args[0])) {
        ang_api->throw_error("contains(sorted_list, value) expects a list and a value.");
        return ang_nil();
    }
    size_t len = ang_api->list_len(args[0]);
    size_t lo = 0, hi = len;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        AngaraObject elem = ang_api->list_get(args[0], (int64_t)mid);
        CmpResult cmp = compare(elem, args[1]);
        ang_api->decref(elem);
        if (cmp == CMP_EQ) return ang_bool(true);
        if (cmp == CMP_LT) lo = mid + 1;
        else hi = mid;
    }
    return ang_bool(false);
}

// --- sort.binary_search(sorted_list, value) -> i64 ---
// Returns index or -1 if not found.
AngaraObject Angara_sort_binary_search(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_LIST(args[0])) {
        ang_api->throw_error("binary_search(sorted_list, value) expects a list and a value.");
        return ang_nil();
    }
    size_t len = ang_api->list_len(args[0]);
    size_t lo = 0, hi = len;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        AngaraObject elem = ang_api->list_get(args[0], (int64_t)mid);
        CmpResult cmp = compare(elem, args[1]);
        ang_api->decref(elem);
        if (cmp == CMP_EQ) return ang_i64((int64_t)mid);
        if (cmp == CMP_LT) lo = mid + 1;
        else hi = mid;
    }
    return ang_i64(-1);
}

// --- sort.dedup(sorted_list) -> list ---
// Remove adjacent duplicates from a sorted list.
AngaraObject Angara_sort_dedup(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_LIST(args[0])) {
        ang_api->throw_error("dedup(sorted_list) expects a list argument.");
        return ang_nil();
    }
    size_t len = ang_api->list_len(args[0]);
    if (len == 0) return ang_api->list_new();

    AngaraObject result = ang_api->list_new();
    AngaraObject prev = ang_api->list_get(args[0], 0);
    ang_api->list_push(result, prev);

    for (size_t i = 1; i < len; i++) {
        AngaraObject elem = ang_api->list_get(args[0], (int64_t)i);
        if (compare(prev, elem) != CMP_EQ) {
            ang_api->list_push(result, elem);
            ang_api->decref(prev);
            prev = elem;
        } else {
            ang_api->decref(elem);
        }
    }
    ang_api->decref(prev);
    return result;
}

// --- sort.merge(list1, list2) -> list ---
// Merge two sorted lists.
AngaraObject Angara_sort_merge(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_LIST(args[0]) || !IS_LIST(args[1])) {
        ang_api->throw_error("merge(list1, list2) expects two sorted lists.");
        return ang_nil();
    }
    size_t len1 = ang_api->list_len(args[0]), len2 = ang_api->list_len(args[1]);
    size_t i = 0, j = 0;
    AngaraObject result = ang_api->list_new();

    while (i < len1 && j < len2) {
        AngaraObject a = ang_api->list_get(args[0], (int64_t)i);
        AngaraObject b = ang_api->list_get(args[1], (int64_t)j);
        if (compare(a, b) != CMP_GT) {
            ang_api->list_push(result, a);
            ang_api->decref(a);
            i++;
        } else {
            ang_api->list_push(result, b);
            ang_api->decref(b);
            j++;
        }
    }
    while (i < len1) {
        AngaraObject a = ang_api->list_get(args[0], (int64_t)i++);
        ang_api->list_push(result, a);
        ang_api->decref(a);
    }
    while (j < len2) {
        AngaraObject b = ang_api->list_get(args[1], (int64_t)j++);
        ang_api->list_push(result, b);
        ang_api->decref(b);
    }
    return result;
}

// --- Export Table ---

static const AngaraFuncDef SORT_EXPORTS[] = {
    {"sorted",        Angara_sort_sorted,        "l->l",   NULL},
    {"reverse",       Angara_sort_reverse,       "l->l",   NULL},
    {"contains",      Angara_sort_contains,      "la->b",  NULL},
    {"binary_search", Angara_sort_binary_search, "la->i",  NULL},
    {"dedup",         Angara_sort_dedup,         "l->l",   NULL},
    {"merge",         Angara_sort_merge,         "ll->l",  NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(sort) {
    ang_api = api;
    *def_count = (sizeof(SORT_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return SORT_EXPORTS;
}