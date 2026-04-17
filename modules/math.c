//
// math.c — Angara math module
//

#include <math.h>
#include <float.h>
#include <limits.h>
#include <stdlib.h>
#include "Angara.h"

// --- Constants ---
// Exposed as functions returning f64 or i64 since the module system exports functions.

AngaraObject Angara_math_PI(int arg_count, AngaraObject* args) {
    return ang_f64(3.14159265358979323846);
}

AngaraObject Angara_math_E(int arg_count, AngaraObject* args) {
    return ang_f64(2.71828182845904523536);
}

AngaraObject Angara_math_TAU(int arg_count, AngaraObject* args) {
    return ang_f64(6.28318530717958647692);
}

AngaraObject Angara_math_INF(int arg_count, AngaraObject* args) {
    return ang_f64(INFINITY);
}

AngaraObject Angara_math_NAN(int arg_count, AngaraObject* args) {
    return ang_f64(NAN);
}

AngaraObject Angara_math_MAX_I64(int arg_count, AngaraObject* args) {
    return ang_i64(INT64_MAX);
}

AngaraObject Angara_math_MIN_I64(int arg_count, AngaraObject* args) {
    return ang_i64(INT64_MIN);
}

AngaraObject Angara_math_MAX_F64(int arg_count, AngaraObject* args) {
    return ang_f64(DBL_MAX);
}

AngaraObject Angara_math_EPSILON(int arg_count, AngaraObject* args) {
    return ang_f64(DBL_EPSILON);
}

// --- Basic Functions ---

AngaraObject Angara_math_abs_i64(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_i64(args[0])) {
        ang_api->throw_error("abs_i64(x) expects one i64 argument.");
        return ang_nil();
    }
    int64_t v = ang_as_i64(args[0]);
    return ang_i64(v < 0 ? -v : v);
}

AngaraObject Angara_math_abs_f64(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_f64(args[0])) {
        ang_api->throw_error("abs_f64(x) expects one f64 argument.");
        return ang_nil();
    }
    return ang_f64(fabs(ang_as_f64(args[0])));
}

AngaraObject Angara_math_sqrt(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_f64(args[0])) {
        ang_api->throw_error("sqrt(x) expects one f64 argument.");
        return ang_nil();
    }
    return ang_f64(sqrt(ang_as_f64(args[0])));
}

AngaraObject Angara_math_cbrt(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_f64(args[0])) {
        ang_api->throw_error("cbrt(x) expects one f64 argument.");
        return ang_nil();
    }
    return ang_f64(cbrt(ang_as_f64(args[0])));
}

AngaraObject Angara_math_pow(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !ang_is_f64(args[0]) || !ang_is_f64(args[1])) {
        ang_api->throw_error("pow(base, exp) expects two f64 arguments.");
        return ang_nil();
    }
    return ang_f64(pow(ang_as_f64(args[0]), ang_as_f64(args[1])));
}

AngaraObject Angara_math_exp(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_f64(args[0])) {
        ang_api->throw_error("exp(x) expects one f64 argument.");
        return ang_nil();
    }
    return ang_f64(exp(ang_as_f64(args[0])));
}

// --- Logarithms ---

AngaraObject Angara_math_log(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_f64(args[0])) {
        ang_api->throw_error("log(x) expects one f64 argument.");
        return ang_nil();
    }
    return ang_f64(log(ang_as_f64(args[0])));
}

AngaraObject Angara_math_log2(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_f64(args[0])) {
        ang_api->throw_error("log2(x) expects one f64 argument.");
        return ang_nil();
    }
    return ang_f64(log2(ang_as_f64(args[0])));
}

AngaraObject Angara_math_log10(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_f64(args[0])) {
        ang_api->throw_error("log10(x) expects one f64 argument.");
        return ang_nil();
    }
    return ang_f64(log10(ang_as_f64(args[0])));
}

// --- Trigonometry ---

AngaraObject Angara_math_sin(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_f64(args[0])) {
        ang_api->throw_error("sin(x) expects one f64 argument.");
        return ang_nil();
    }
    return ang_f64(sin(ang_as_f64(args[0])));
}

AngaraObject Angara_math_cos(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_f64(args[0])) {
        ang_api->throw_error("cos(x) expects one f64 argument.");
        return ang_nil();
    }
    return ang_f64(cos(ang_as_f64(args[0])));
}

AngaraObject Angara_math_tan(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_f64(args[0])) {
        ang_api->throw_error("tan(x) expects one f64 argument.");
        return ang_nil();
    }
    return ang_f64(tan(ang_as_f64(args[0])));
}

AngaraObject Angara_math_asin(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_f64(args[0])) {
        ang_api->throw_error("asin(x) expects one f64 argument.");
        return ang_nil();
    }
    return ang_f64(asin(ang_as_f64(args[0])));
}

AngaraObject Angara_math_acos(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_f64(args[0])) {
        ang_api->throw_error("acos(x) expects one f64 argument.");
        return ang_nil();
    }
    return ang_f64(acos(ang_as_f64(args[0])));
}

AngaraObject Angara_math_atan(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_f64(args[0])) {
        ang_api->throw_error("atan(x) expects one f64 argument.");
        return ang_nil();
    }
    return ang_f64(atan(ang_as_f64(args[0])));
}

AngaraObject Angara_math_atan2(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !ang_is_f64(args[0]) || !ang_is_f64(args[1])) {
        ang_api->throw_error("atan2(y, x) expects two f64 arguments.");
        return ang_nil();
    }
    return ang_f64(atan2(ang_as_f64(args[0]), ang_as_f64(args[1])));
}

AngaraObject Angara_math_hypot(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !ang_is_f64(args[0]) || !ang_is_f64(args[1])) {
        ang_api->throw_error("hypot(x, y) expects two f64 arguments.");
        return ang_nil();
    }
    return ang_f64(hypot(ang_as_f64(args[0]), ang_as_f64(args[1])));
}

// --- Rounding ---

AngaraObject Angara_math_ceil(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_f64(args[0])) {
        ang_api->throw_error("ceil(x) expects one f64 argument.");
        return ang_nil();
    }
    return ang_f64(ceil(ang_as_f64(args[0])));
}

AngaraObject Angara_math_floor(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_f64(args[0])) {
        ang_api->throw_error("floor(x) expects one f64 argument.");
        return ang_nil();
    }
    return ang_f64(floor(ang_as_f64(args[0])));
}

AngaraObject Angara_math_round(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_f64(args[0])) {
        ang_api->throw_error("round(x) expects one f64 argument.");
        return ang_nil();
    }
    return ang_f64(round(ang_as_f64(args[0])));
}

AngaraObject Angara_math_trunc(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_f64(args[0])) {
        ang_api->throw_error("trunc(x) expects one f64 argument.");
        return ang_nil();
    }
    return ang_f64(trunc(ang_as_f64(args[0])));
}

// --- Min / Max / Clamp ---

AngaraObject Angara_math_min(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !ang_is_f64(args[0]) || !ang_is_f64(args[1])) {
        ang_api->throw_error("min(a, b) expects two f64 arguments.");
        return ang_nil();
    }
    double a = ang_as_f64(args[0]), b = ang_as_f64(args[1]);
    return ang_f64(a < b ? a : b);
}

AngaraObject Angara_math_max(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !ang_is_f64(args[0]) || !ang_is_f64(args[1])) {
        ang_api->throw_error("max(a, b) expects two f64 arguments.");
        return ang_nil();
    }
    double a = ang_as_f64(args[0]), b = ang_as_f64(args[1]);
    return ang_f64(a > b ? a : b);
}

AngaraObject Angara_math_clamp(int arg_count, AngaraObject* args) {
    if (arg_count != 3 || !ang_is_f64(args[0]) || !ang_is_f64(args[1]) || !ang_is_f64(args[2])) {
        ang_api->throw_error("clamp(val, lo, hi) expects three f64 arguments.");
        return ang_nil();
    }
    double v = ang_as_f64(args[0]), lo = ang_as_f64(args[1]), hi = ang_as_f64(args[2]);
    if (v < lo) return ang_f64(lo);
    if (v > hi) return ang_f64(hi);
    return ang_f64(v);
}

// --- Utility ---

AngaraObject Angara_math_sign(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_f64(args[0])) {
        ang_api->throw_error("sign(x) expects one f64 argument.");
        return ang_nil();
    }
    double v = ang_as_f64(args[0]);
    if (v > 0.0) return ang_i64(1);
    if (v < 0.0) return ang_i64(-1);
    return ang_i64(0);
}

AngaraObject Angara_math_to_radians(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_f64(args[0])) {
        ang_api->throw_error("to_radians(degrees) expects one f64 argument.");
        return ang_nil();
    }
    return ang_f64(ang_as_f64(args[0]) * 3.14159265358979323846 / 180.0);
}

AngaraObject Angara_math_to_degrees(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_f64(args[0])) {
        ang_api->throw_error("to_degrees(radians) expects one f64 argument.");
        return ang_nil();
    }
    return ang_f64(ang_as_f64(args[0]) * 180.0 / 3.14159265358979323846);
}

AngaraObject Angara_math_is_nan(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_f64(args[0])) {
        ang_api->throw_error("is_nan(x) expects one f64 argument.");
        return ang_nil();
    }
    return ang_bool(isnan(ang_as_f64(args[0])));
}

AngaraObject Angara_math_is_infinite(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_f64(args[0])) {
        ang_api->throw_error("is_infinite(x) expects one f64 argument.");
        return ang_nil();
    }
    return ang_bool(isinf(ang_as_f64(args[0])));
}

AngaraObject Angara_math_lerp(int arg_count, AngaraObject* args) {
    if (arg_count != 3 || !ang_is_f64(args[0]) || !ang_is_f64(args[1]) || !ang_is_f64(args[2])) {
        ang_api->throw_error("lerp(a, b, t) expects three f64 arguments.");
        return ang_nil();
    }
    double a = ang_as_f64(args[0]), b = ang_as_f64(args[1]), t = ang_as_f64(args[2]);
    return ang_f64(a + (b - a) * t);
}

// --- Statistics ---

AngaraObject Angara_math_sum(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_obj(args[0]) || ang_api->obj_type(args[0]) != ANG_OBJ_LIST) {
        ang_api->throw_error("sum(list) expects a list of numbers.");
        return ang_nil();
    }
    size_t len = ang_api->list_len(args[0]);
    double total = 0.0;
    for (size_t i = 0; i < len; i++) {
        AngaraObject elem = ang_api->list_get(args[0], (int64_t)i);
        if (ang_is_f64(elem)) total += ang_as_f64(elem);
        else if (ang_is_i64(elem)) total += (double)ang_as_i64(elem);
        ang_api->decref(elem);
    }
    return ang_f64(total);
}

AngaraObject Angara_math_mean(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_obj(args[0]) || ang_api->obj_type(args[0]) != ANG_OBJ_LIST) {
        ang_api->throw_error("mean(list) expects a list of numbers.");
        return ang_nil();
    }
    size_t len = ang_api->list_len(args[0]);
    if (len == 0) { ang_api->throw_error("mean: empty list."); return ang_nil(); }
    double total = 0.0;
    for (size_t i = 0; i < len; i++) {
        AngaraObject elem = ang_api->list_get(args[0], (int64_t)i);
        if (ang_is_f64(elem)) total += ang_as_f64(elem);
        else if (ang_is_i64(elem)) total += (double)ang_as_i64(elem);
        ang_api->decref(elem);
    }
    return ang_f64(total / (double)len);
}

// Helper: extract sorted f64 array from a list
static double* extract_sorted_doubles(AngaraObject list, size_t* out_len) {
    size_t len = ang_api->list_len(list);
    if (len == 0) { *out_len = 0; return NULL; }
    double* arr = (double*)malloc(len * sizeof(double));
    if (!arr) { *out_len = 0; return NULL; }
    for (size_t i = 0; i < len; i++) {
        AngaraObject elem = ang_api->list_get(list, (int64_t)i);
        if (ang_is_f64(elem)) arr[i] = ang_as_f64(elem);
        else if (ang_is_i64(elem)) arr[i] = (double)ang_as_i64(elem);
        else arr[i] = 0.0;
        ang_api->decref(elem);
    }
    // Simple insertion sort
    for (size_t i = 1; i < len; i++) {
        double key = arr[i];
        size_t j = i;
        while (j > 0 && arr[j - 1] > key) { arr[j] = arr[j - 1]; j--; }
        arr[j] = key;
    }
    *out_len = len;
    return arr;
}

AngaraObject Angara_math_median(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_obj(args[0]) || ang_api->obj_type(args[0]) != ANG_OBJ_LIST) {
        ang_api->throw_error("median(list) expects a list of numbers.");
        return ang_nil();
    }
    size_t len;
    double* arr = extract_sorted_doubles(args[0], &len);
    if (!arr || len == 0) { ang_api->throw_error("median: empty list."); return ang_nil(); }
    double result;
    if (len % 2 == 0) result = (arr[len / 2 - 1] + arr[len / 2]) / 2.0;
    else result = arr[len / 2];
    free(arr);
    return ang_f64(result);
}

AngaraObject Angara_math_variance(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_obj(args[0]) || ang_api->obj_type(args[0]) != ANG_OBJ_LIST) {
        ang_api->throw_error("variance(list) expects a list of numbers.");
        return ang_nil();
    }
    size_t len = ang_api->list_len(args[0]);
    if (len < 2) { ang_api->throw_error("variance: need at least 2 elements."); return ang_nil(); }
    double total = 0.0;
    for (size_t i = 0; i < len; i++) {
        AngaraObject elem = ang_api->list_get(args[0], (int64_t)i);
        if (ang_is_f64(elem)) total += ang_as_f64(elem);
        else if (ang_is_i64(elem)) total += (double)ang_as_i64(elem);
        ang_api->decref(elem);
    }
    double mean = total / (double)len;
    double sum_sq = 0.0;
    for (size_t i = 0; i < len; i++) {
        AngaraObject elem = ang_api->list_get(args[0], (int64_t)i);
        double v = 0.0;
        if (ang_is_f64(elem)) v = ang_as_f64(elem);
        else if (ang_is_i64(elem)) v = (double)ang_as_i64(elem);
        double diff = v - mean;
        sum_sq += diff * diff;
        ang_api->decref(elem);
    }
    return ang_f64(sum_sq / (double)(len - 1)); // sample variance
}

AngaraObject Angara_math_std_dev(int arg_count, AngaraObject* args) {
    AngaraObject v = Angara_math_variance(arg_count, args);
    if (ang_is_f64(v)) return ang_f64(sqrt(ang_as_f64(v)));
    return v;
}

AngaraObject Angara_math_min_by(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_obj(args[0]) || ang_api->obj_type(args[0]) != ANG_OBJ_LIST) {
        ang_api->throw_error("min_by(list) expects a list of numbers.");
        return ang_nil();
    }
    size_t len = ang_api->list_len(args[0]);
    if (len == 0) { ang_api->throw_error("min_by: empty list."); return ang_nil(); }
    AngaraObject best = ang_api->list_get(args[0], 0);
    double best_val = 0.0;
    if (ang_is_f64(best)) best_val = ang_as_f64(best);
    else if (ang_is_i64(best)) best_val = (double)ang_as_i64(best);
    for (size_t i = 1; i < len; i++) {
        AngaraObject elem = ang_api->list_get(args[0], (int64_t)i);
        double v = 0.0;
        if (ang_is_f64(elem)) v = ang_as_f64(elem);
        else if (ang_is_i64(elem)) v = (double)ang_as_i64(elem);
        if (v < best_val) { ang_api->decref(best); best = elem; best_val = v; }
        else ang_api->decref(elem);
    }
    return best;
}

AngaraObject Angara_math_max_by(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_obj(args[0]) || ang_api->obj_type(args[0]) != ANG_OBJ_LIST) {
        ang_api->throw_error("max_by(list) expects a list of numbers.");
        return ang_nil();
    }
    size_t len = ang_api->list_len(args[0]);
    if (len == 0) { ang_api->throw_error("max_by: empty list."); return ang_nil(); }
    AngaraObject best = ang_api->list_get(args[0], 0);
    double best_val = 0.0;
    if (ang_is_f64(best)) best_val = ang_as_f64(best);
    else if (ang_is_i64(best)) best_val = (double)ang_as_i64(best);
    for (size_t i = 1; i < len; i++) {
        AngaraObject elem = ang_api->list_get(args[0], (int64_t)i);
        double v = 0.0;
        if (ang_is_f64(elem)) v = ang_as_f64(elem);
        else if (ang_is_i64(elem)) v = (double)ang_as_i64(elem);
        if (v > best_val) { ang_api->decref(best); best = elem; best_val = v; }
        else ang_api->decref(elem);
    }
    return best;
}

// --- Number Theory ---

static int64_t gcd_impl(int64_t a, int64_t b) {
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b != 0) { int64_t t = b; b = a % b; a = t; }
    return a;
}

AngaraObject Angara_math_gcd(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !ang_is_i64(args[0]) || !ang_is_i64(args[1])) {
        ang_api->throw_error("gcd(a, b) expects two i64 arguments.");
        return ang_nil();
    }
    return ang_i64(gcd_impl(ang_as_i64(args[0]), ang_as_i64(args[1])));
}

AngaraObject Angara_math_lcm(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !ang_is_i64(args[0]) || !ang_is_i64(args[1])) {
        ang_api->throw_error("lcm(a, b) expects two i64 arguments.");
        return ang_nil();
    }
    int64_t a = ang_as_i64(args[0]), b = ang_as_i64(args[1]);
    if (a == 0 || b == 0) return ang_i64(0);
    int64_t g = gcd_impl(a, b);
    int64_t la = a < 0 ? -a : a;
    int64_t lb = b < 0 ? -b : b;
    return ang_i64((la / g) * lb);
}

AngaraObject Angara_math_factorial(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_i64(args[0])) {
        ang_api->throw_error("factorial(n) expects one i64 argument.");
        return ang_nil();
    }
    int64_t n = ang_as_i64(args[0]);
    if (n < 0) { ang_api->throw_error("factorial: n must be >= 0."); return ang_nil(); }
    if (n > 20) { ang_api->throw_error("factorial: overflow for n > 20."); return ang_nil(); }
    int64_t result = 1;
    for (int64_t i = 2; i <= n; i++) result *= i;
    return ang_i64(result);
}

AngaraObject Angara_math_fibonacci(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_i64(args[0])) {
        ang_api->throw_error("fibonacci(n) expects one i64 argument.");
        return ang_nil();
    }
    int64_t n = ang_as_i64(args[0]);
    if (n < 0) { ang_api->throw_error("fibonacci: n must be >= 0."); return ang_nil(); }
    if (n == 0) return ang_i64(0);
    if (n == 1) return ang_i64(1);
    int64_t a = 0, b = 1;
    for (int64_t i = 2; i <= n; i++) {
        int64_t c = a + b;
        a = b;
        b = c;
    }
    return ang_i64(b);
}

AngaraObject Angara_math_is_prime(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !ang_is_i64(args[0])) {
        ang_api->throw_error("is_prime(n) expects one i64 argument.");
        return ang_nil();
    }
    int64_t n = ang_as_i64(args[0]);
    if (n < 2) return ang_bool(false);
    if (n < 4) return ang_bool(true);
    if (n % 2 == 0 || n % 3 == 0) return ang_bool(false);
    for (int64_t i = 5; i * i <= n; i += 6) {
        if (n % i == 0 || n % (i + 2) == 0) return ang_bool(false);
    }
    return ang_bool(true);
}

// --- Export Table ---

static const AngaraFuncDef MATH_EXPORTS[] = {
    // Constants
    {"PI",          Angara_math_PI,          "->d",    NULL},
    {"E",           Angara_math_E,           "->d",    NULL},
    {"TAU",         Angara_math_TAU,         "->d",    NULL},
    {"INF",         Angara_math_INF,         "->d",    NULL},
    {"NAN",         Angara_math_NAN,         "->d",    NULL},
    {"MAX_I64",     Angara_math_MAX_I64,     "->i",    NULL},
    {"MIN_I64",     Angara_math_MIN_I64,     "->i",    NULL},
    {"MAX_F64",     Angara_math_MAX_F64,     "->d",    NULL},
    {"EPSILON",     Angara_math_EPSILON,     "->d",    NULL},
    // Basic
    {"abs_i64",     Angara_math_abs_i64,     "i->i",   NULL},
    {"abs_f64",     Angara_math_abs_f64,     "d->d",   NULL},
    {"sqrt",        Angara_math_sqrt,        "d->d",   NULL},
    {"cbrt",        Angara_math_cbrt,        "d->d",   NULL},
    {"pow",         Angara_math_pow,         "dd->d",  NULL},
    {"exp",         Angara_math_exp,         "d->d",   NULL},
    // Logarithms
    {"log",         Angara_math_log,         "d->d",   NULL},
    {"log2",        Angara_math_log2,        "d->d",   NULL},
    {"log10",       Angara_math_log10,       "d->d",   NULL},
    // Trigonometry
    {"sin",         Angara_math_sin,         "d->d",   NULL},
    {"cos",         Angara_math_cos,         "d->d",   NULL},
    {"tan",         Angara_math_tan,         "d->d",   NULL},
    {"asin",        Angara_math_asin,        "d->d",   NULL},
    {"acos",        Angara_math_acos,        "d->d",   NULL},
    {"atan",        Angara_math_atan,        "d->d",   NULL},
    {"atan2",       Angara_math_atan2,       "dd->d",  NULL},
    {"hypot",       Angara_math_hypot,       "dd->d",  NULL},
    // Rounding
    {"ceil",        Angara_math_ceil,        "d->d",   NULL},
    {"floor",       Angara_math_floor,       "d->d",   NULL},
    {"round",       Angara_math_round,       "d->d",   NULL},
    {"trunc",       Angara_math_trunc,       "d->d",   NULL},
    // Min/Max/Clamp
    {"min",         Angara_math_min,         "dd->d",  NULL},
    {"max",         Angara_math_max,         "dd->d",  NULL},
    {"clamp",       Angara_math_clamp,       "ddd->d", NULL},
    // Utility
    {"sign",        Angara_math_sign,        "d->i",   NULL},
    {"to_radians",  Angara_math_to_radians,  "d->d",   NULL},
    {"to_degrees",  Angara_math_to_degrees,  "d->d",   NULL},
    {"is_nan",      Angara_math_is_nan,      "d->b",   NULL},
    {"is_infinite", Angara_math_is_infinite, "d->b",   NULL},
    {"lerp",        Angara_math_lerp,        "ddd->d", NULL},
    // Statistics
    {"sum",         Angara_math_sum,         "l<d>->d",  NULL},
    {"mean",        Angara_math_mean,        "l<d>->d",  NULL},
    {"median",      Angara_math_median,      "l<d>->d",  NULL},
    {"variance",    Angara_math_variance,    "l<d>->d",  NULL},
    {"std_dev",     Angara_math_std_dev,     "l<d>->d",  NULL},
    {"min_by",      Angara_math_min_by,      "l<d>->d",  NULL},
    {"max_by",      Angara_math_max_by,      "l<d>->d",  NULL},
    // Number Theory
    {"gcd",         Angara_math_gcd,         "ii->i",    NULL},
    {"lcm",         Angara_math_lcm,         "ii->i",    NULL},
    {"factorial",   Angara_math_factorial,   "i->i",     NULL},
    {"fibonacci",   Angara_math_fibonacci,   "i->i",     NULL},
    {"is_prime",    Angara_math_is_prime,    "i->b",     NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(math) {
    ang_api = api;
    *def_count = (sizeof(MATH_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return MATH_EXPORTS;
}