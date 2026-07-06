/// Angara bigint module — arbitrary-precision integers via GMP.
///
/// Provides a `BigInt` native class wrapping GMP's mpz_t.  Constructors:
///   bigint.from_i64(i64)        -> BigInt
///   bigint.from_string(string)  -> BigInt
///
/// Methods (arithmetic uses method syntax, comparisons use operators):
///   add, sub, mul, div, rem, neg, abs, to_string, to_i64,
///   opCmp (enables < <= > >=), opEquals (enables == !=)
///
/// Depends: GMP (libgmp).

#include <gmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "Angara.h"

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

typedef struct {
    mpz_t value;
} BigInt;

static void bigint_finalize(void* data) {
    BigInt* bi = (BigInt*)data;
    mpz_clear(bi->value);
    free(bi);
}

static BigInt* bigint_unwrap(AngaraObject obj) {
    return (BigInt*)ang_api->native_instance_data(obj);
}

// Require that arg[idx] is a BigInt; unwrap and return its mpz_t.
static mpz_ptr bigint_arg(int argc, AngaraObject* args, int idx) {
    BigInt* bi = bigint_unwrap(args[idx]);
    if (!bi) {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "bigint: argument %d is not a BigInt (maybe it was collected?).", idx);
        ang_api->throw_error(buf);
        return NULL;
    }
    return bi->value;
}

// ---------------------------------------------------------------------------
// Constructors (module-level functions)
// ---------------------------------------------------------------------------

/// bigint.from_i64(n: i64) -> BigInt
AngaraObject Angara_bigint_from_i64(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("bigint.from_i64: expected 1 argument"); return ang_nil(); }
    BigInt* bi = (BigInt*)malloc(sizeof(BigInt));
    if (!bi) { ang_api->throw_error("bigint: out of memory."); return ang_nil(); }
    mpz_init_set_si(bi->value, ang_as_i64(args[0]));
    return ang_api->native_instance_new(bi, bigint_finalize, "BigInt");
}

/// bigint.from_string(s: string) -> BigInt
AngaraObject Angara_bigint_from_string(int arg_count, AngaraObject* args) {
    if (arg_count < 1) { ang_api->throw_error("bigint.from_string: expected 1 argument"); return ang_nil(); }
    BigInt* bi = (BigInt*)malloc(sizeof(BigInt));
    if (!bi) { ang_api->throw_error("bigint: out of memory."); return ang_nil(); }
    if (mpz_init_set_str(bi->value, ang_api->as_cstr(args[0]), 10) != 0) {
        mpz_clear(bi->value);
        free(bi);
        ang_api->throw_error("bigint.from_string: invalid integer string.");
        return ang_nil();
    }
    return ang_api->native_instance_new(bi, bigint_finalize, "BigInt");
}

// ---------------------------------------------------------------------------
// Arithmetic methods (BigInt -> BigInt, returns new BigInt)
// ---------------------------------------------------------------------------

/// add(self, other: BigInt) -> BigInt
AngaraObject Angara_BigInt_add(int arg_count, AngaraObject* args) {
    mpz_ptr a = bigint_arg(arg_count, args, 0);
    mpz_ptr b = bigint_arg(arg_count, args, 1);
    if (!a || !b) return ang_nil();
    BigInt* r = (BigInt*)malloc(sizeof(BigInt));
    if (!r) { ang_api->throw_error("bigint: out of memory."); return ang_nil(); }
    mpz_init(r->value);
    mpz_add(r->value, a, b);
    return ang_api->native_instance_new(r, bigint_finalize, "BigInt");
}

/// sub(self, other: BigInt) -> BigInt
AngaraObject Angara_BigInt_sub(int arg_count, AngaraObject* args) {
    mpz_ptr a = bigint_arg(arg_count, args, 0);
    mpz_ptr b = bigint_arg(arg_count, args, 1);
    if (!a || !b) return ang_nil();
    BigInt* r = (BigInt*)malloc(sizeof(BigInt));
    if (!r) { ang_api->throw_error("bigint: out of memory."); return ang_nil(); }
    mpz_init(r->value);
    mpz_sub(r->value, a, b);
    return ang_api->native_instance_new(r, bigint_finalize, "BigInt");
}

/// mul(self, other: BigInt) -> BigInt
AngaraObject Angara_BigInt_mul(int arg_count, AngaraObject* args) {
    mpz_ptr a = bigint_arg(arg_count, args, 0);
    mpz_ptr b = bigint_arg(arg_count, args, 1);
    if (!a || !b) return ang_nil();
    BigInt* r = (BigInt*)malloc(sizeof(BigInt));
    if (!r) { ang_api->throw_error("bigint: out of memory."); return ang_nil(); }
    mpz_init(r->value);
    mpz_mul(r->value, a, b);
    return ang_api->native_instance_new(r, bigint_finalize, "BigInt");
}

/// div(self, other: BigInt) -> BigInt   (truncating division)
AngaraObject Angara_BigInt_div(int arg_count, AngaraObject* args) {
    mpz_ptr a = bigint_arg(arg_count, args, 0);
    mpz_ptr b = bigint_arg(arg_count, args, 1);
    if (!a || !b) return ang_nil();
    if (mpz_sgn(b) == 0) { ang_api->throw_error("bigint: division by zero."); return ang_nil(); }
    BigInt* r = (BigInt*)malloc(sizeof(BigInt));
    if (!r) { ang_api->throw_error("bigint: out of memory."); return ang_nil(); }
    mpz_init(r->value);
    mpz_tdiv_q(r->value, a, b);
    return ang_api->native_instance_new(r, bigint_finalize, "BigInt");
}

/// rem(self, other: BigInt) -> BigInt
AngaraObject Angara_BigInt_rem(int arg_count, AngaraObject* args) {
    mpz_ptr a = bigint_arg(arg_count, args, 0);
    mpz_ptr b = bigint_arg(arg_count, args, 1);
    if (!a || !b) return ang_nil();
    if (mpz_sgn(b) == 0) { ang_api->throw_error("bigint: remainder by zero."); return ang_nil(); }
    BigInt* r = (BigInt*)malloc(sizeof(BigInt));
    if (!r) { ang_api->throw_error("bigint: out of memory."); return ang_nil(); }
    mpz_init(r->value);
    mpz_tdiv_r(r->value, a, b);
    return ang_api->native_instance_new(r, bigint_finalize, "BigInt");
}

// ---------------------------------------------------------------------------
// Unary methods
// ---------------------------------------------------------------------------

/// neg(self) -> BigInt
AngaraObject Angara_BigInt_neg(int arg_count, AngaraObject* args) {
    mpz_ptr a = bigint_arg(arg_count, args, 0);
    if (!a) return ang_nil();
    BigInt* r = (BigInt*)malloc(sizeof(BigInt));
    if (!r) { ang_api->throw_error("bigint: out of memory."); return ang_nil(); }
    mpz_init(r->value);
    mpz_neg(r->value, a);
    return ang_api->native_instance_new(r, bigint_finalize, "BigInt");
}

/// abs(self) -> BigInt
AngaraObject Angara_BigInt_abs(int arg_count, AngaraObject* args) {
    mpz_ptr a = bigint_arg(arg_count, args, 0);
    if (!a) return ang_nil();
    BigInt* r = (BigInt*)malloc(sizeof(BigInt));
    if (!r) { ang_api->throw_error("bigint: out of memory."); return ang_nil(); }
    mpz_init(r->value);
    mpz_abs(r->value, a);
    return ang_api->native_instance_new(r, bigint_finalize, "BigInt");
}

// ---------------------------------------------------------------------------
// Conversion methods
// ---------------------------------------------------------------------------

/// to_string(self) -> string
AngaraObject Angara_BigInt_to_string(int arg_count, AngaraObject* args) {
    mpz_ptr a = bigint_arg(arg_count, args, 0);
    if (!a) return ang_nil();
    char* str = mpz_get_str(NULL, 10, a);
    if (!str) { ang_api->throw_error("bigint: out of memory."); return ang_nil(); }
    AngaraObject result = ang_api->string(str);
    free(str);
    return result;
}

/// to_i64(self) -> i64   (returns nil if out of range)
AngaraObject Angara_BigInt_to_i64(int arg_count, AngaraObject* args) {
    mpz_ptr a = bigint_arg(arg_count, args, 0);
    if (!a) return ang_nil();
    if (!mpz_fits_slong_p(a)) return ang_nil();
    return ang_i64((int64_t)mpz_get_si(a));
}

// ---------------------------------------------------------------------------
// Comparison operators (LANG-13: enable == != < <= > >= via opEquals / opCmp)
// ---------------------------------------------------------------------------

/// opEquals(self, other: BigInt) -> bool
AngaraObject Angara_BigInt_opEquals(int arg_count, AngaraObject* args) {
    mpz_ptr a = bigint_arg(arg_count, args, 0);
    mpz_ptr b = bigint_arg(arg_count, args, 1);
    if (!a || !b) return ang_bool(false);
    return ang_bool(mpz_cmp(a, b) == 0);
}

/// opCmp(self, other: BigInt) -> i64   (-1 if self < other, 0 if ==, 1 if >)
AngaraObject Angara_BigInt_opCmp(int arg_count, AngaraObject* args) {
    mpz_ptr a = bigint_arg(arg_count, args, 0);
    mpz_ptr b = bigint_arg(arg_count, args, 1);
    if (!a || !b) return ang_i64(0);
    int c = mpz_cmp(a, b);
    return ang_i64(c < 0 ? -1 : (c > 0 ? 1 : 0));
}

// ---------------------------------------------------------------------------
// Operator-overload wrappers (LANG-13: enable + - * / % and unary -)
// These delegate to the existing arithmetic methods.
// ---------------------------------------------------------------------------

AngaraObject Angara_BigInt_opAdd(int arg_count, AngaraObject* args) {
    return Angara_BigInt_add(arg_count, args);
}
AngaraObject Angara_BigInt_opSub(int arg_count, AngaraObject* args) {
    return Angara_BigInt_sub(arg_count, args);
}
AngaraObject Angara_BigInt_opMul(int arg_count, AngaraObject* args) {
    return Angara_BigInt_mul(arg_count, args);
}
AngaraObject Angara_BigInt_opDiv(int arg_count, AngaraObject* args) {
    return Angara_BigInt_div(arg_count, args);
}
AngaraObject Angara_BigInt_opRem(int arg_count, AngaraObject* args) {
    return Angara_BigInt_rem(arg_count, args);
}
AngaraObject Angara_BigInt_opNeg(int arg_count, AngaraObject* args) {
    return Angara_BigInt_neg(arg_count, args);
}

// ---------------------------------------------------------------------------
// Additional GMP-backed methods
// ---------------------------------------------------------------------------

/// pow(self, exp: BigInt) -> BigInt   (exponent must be >= 0 and fit in unsigned long)
AngaraObject Angara_BigInt_pow(int arg_count, AngaraObject* args) {
    mpz_ptr a = bigint_arg(arg_count, args, 0);
    mpz_ptr b = bigint_arg(arg_count, args, 1);
    if (!a || !b) return ang_nil();
    if (mpz_sgn(b) < 0) { ang_api->throw_error("bigint.pow: negative exponent is not supported."); return ang_nil(); }
    if (!mpz_fits_ulong_p(b)) { ang_api->throw_error("bigint.pow: exponent too large."); return ang_nil(); }
    unsigned long exp = mpz_get_ui(b);
    BigInt* r = (BigInt*)malloc(sizeof(BigInt));
    if (!r) { ang_api->throw_error("bigint: out of memory."); return ang_nil(); }
    mpz_init(r->value);
    mpz_pow_ui(r->value, a, exp);
    return ang_api->native_instance_new(r, bigint_finalize, "BigInt");
}

/// gcd(self, other: BigInt) -> BigInt
AngaraObject Angara_BigInt_gcd(int arg_count, AngaraObject* args) {
    mpz_ptr a = bigint_arg(arg_count, args, 0);
    mpz_ptr b = bigint_arg(arg_count, args, 1);
    if (!a || !b) return ang_nil();
    BigInt* r = (BigInt*)malloc(sizeof(BigInt));
    if (!r) { ang_api->throw_error("bigint: out of memory."); return ang_nil(); }
    mpz_init(r->value);
    mpz_gcd(r->value, a, b);
    return ang_api->native_instance_new(r, bigint_finalize, "BigInt");
}

/// lcm(self, other: BigInt) -> BigInt
AngaraObject Angara_BigInt_lcm(int arg_count, AngaraObject* args) {
    mpz_ptr a = bigint_arg(arg_count, args, 0);
    mpz_ptr b = bigint_arg(arg_count, args, 1);
    if (!a || !b) return ang_nil();
    BigInt* r = (BigInt*)malloc(sizeof(BigInt));
    if (!r) { ang_api->throw_error("bigint: out of memory."); return ang_nil(); }
    mpz_init(r->value);
    mpz_lcm(r->value, a, b);
    return ang_api->native_instance_new(r, bigint_finalize, "BigInt");
}

/// sqrt(self) -> BigInt   (integer truncating square root)
AngaraObject Angara_BigInt_sqrt(int arg_count, AngaraObject* args) {
    mpz_ptr a = bigint_arg(arg_count, args, 0);
    if (!a) return ang_nil();
    if (mpz_sgn(a) < 0) { ang_api->throw_error("bigint.sqrt: negative argument."); return ang_nil(); }
    BigInt* r = (BigInt*)malloc(sizeof(BigInt));
    if (!r) { ang_api->throw_error("bigint: out of memory."); return ang_nil(); }
    mpz_init(r->value);
    mpz_sqrt(r->value, a);
    return ang_api->native_instance_new(r, bigint_finalize, "BigInt");
}

/// bit_and(self, other: BigInt) -> BigInt
AngaraObject Angara_BigInt_bit_and(int arg_count, AngaraObject* args) {
    mpz_ptr a = bigint_arg(arg_count, args, 0);
    mpz_ptr b = bigint_arg(arg_count, args, 1);
    if (!a || !b) return ang_nil();
    BigInt* r = (BigInt*)malloc(sizeof(BigInt));
    if (!r) { ang_api->throw_error("bigint: out of memory."); return ang_nil(); }
    mpz_init(r->value);
    mpz_and(r->value, a, b);
    return ang_api->native_instance_new(r, bigint_finalize, "BigInt");
}

/// bit_or(self, other: BigInt) -> BigInt
AngaraObject Angara_BigInt_bit_or(int arg_count, AngaraObject* args) {
    mpz_ptr a = bigint_arg(arg_count, args, 0);
    mpz_ptr b = bigint_arg(arg_count, args, 1);
    if (!a || !b) return ang_nil();
    BigInt* r = (BigInt*)malloc(sizeof(BigInt));
    if (!r) { ang_api->throw_error("bigint: out of memory."); return ang_nil(); }
    mpz_init(r->value);
    mpz_ior(r->value, a, b);
    return ang_api->native_instance_new(r, bigint_finalize, "BigInt");
}

/// bit_xor(self, other: BigInt) -> BigInt
AngaraObject Angara_BigInt_bit_xor(int arg_count, AngaraObject* args) {
    mpz_ptr a = bigint_arg(arg_count, args, 0);
    mpz_ptr b = bigint_arg(arg_count, args, 1);
    if (!a || !b) return ang_nil();
    BigInt* r = (BigInt*)malloc(sizeof(BigInt));
    if (!r) { ang_api->throw_error("bigint: out of memory."); return ang_nil(); }
    mpz_init(r->value);
    mpz_xor(r->value, a, b);
    return ang_api->native_instance_new(r, bigint_finalize, "BigInt");
}

/// shift_left(self, n: i64) -> BigInt   (shift left by n bits; n must be >= 0)
AngaraObject Angara_BigInt_shift_left(int arg_count, AngaraObject* args) {
    if (arg_count < 2) { ang_api->throw_error("BigInt.shift_left: expected 2 arguments"); return ang_nil(); }
    mpz_ptr a = bigint_arg(arg_count, args, 0);
    if (!a) return ang_nil();
    int64_t n = ang_as_i64(args[1]);
    if (n < 0) { ang_api->throw_error("bigint.shift_left: negative shift count."); return ang_nil(); }
    BigInt* r = (BigInt*)malloc(sizeof(BigInt));
    if (!r) { ang_api->throw_error("bigint: out of memory."); return ang_nil(); }
    mpz_init(r->value);
    mpz_mul_2exp(r->value, a, (mp_bitcnt_t)n);
    return ang_api->native_instance_new(r, bigint_finalize, "BigInt");
}

/// shift_right(self, n: i64) -> BigInt   (shift right by n bits; n must be >= 0)
AngaraObject Angara_BigInt_shift_right(int arg_count, AngaraObject* args) {
    if (arg_count < 2) { ang_api->throw_error("BigInt.shift_right: expected 2 arguments"); return ang_nil(); }
    mpz_ptr a = bigint_arg(arg_count, args, 0);
    if (!a) return ang_nil();
    int64_t n = ang_as_i64(args[1]);
    if (n < 0) { ang_api->throw_error("bigint.shift_right: negative shift count."); return ang_nil(); }
    BigInt* r = (BigInt*)malloc(sizeof(BigInt));
    if (!r) { ang_api->throw_error("bigint: out of memory."); return ang_nil(); }
    mpz_init(r->value);
    mpz_tdiv_q_2exp(r->value, a, (mp_bitcnt_t)n);
    return ang_api->native_instance_new(r, bigint_finalize, "BigInt");
}

/// sign(self) -> i64   (-1 if negative, 0 if zero, 1 if positive)
AngaraObject Angara_BigInt_sign(int arg_count, AngaraObject* args) {
    mpz_ptr a = bigint_arg(arg_count, args, 0);
    if (!a) return ang_i64(0);
    return ang_i64((int64_t)mpz_sgn(a));
}

// ---------------------------------------------------------------------------
// Export tables
// ---------------------------------------------------------------------------

static const AngaraMethodDef BIGINT_METHODS[] = {
    {"add",       (AngaraMethodFn)Angara_BigInt_add,       "BigInt->BigInt"},
    {"sub",       (AngaraMethodFn)Angara_BigInt_sub,       "BigInt->BigInt"},
    {"mul",       (AngaraMethodFn)Angara_BigInt_mul,       "BigInt->BigInt"},
    {"div",       (AngaraMethodFn)Angara_BigInt_div,       "BigInt->BigInt"},
    {"rem",       (AngaraMethodFn)Angara_BigInt_rem,       "BigInt->BigInt"},
    {"neg",       (AngaraMethodFn)Angara_BigInt_neg,       "->BigInt"},
    {"abs",       (AngaraMethodFn)Angara_BigInt_abs,       "->BigInt"},
    {"pow",       (AngaraMethodFn)Angara_BigInt_pow,       "BigInt->BigInt"},
    {"gcd",       (AngaraMethodFn)Angara_BigInt_gcd,       "BigInt->BigInt"},
    {"lcm",       (AngaraMethodFn)Angara_BigInt_lcm,       "BigInt->BigInt"},
    {"sqrt",      (AngaraMethodFn)Angara_BigInt_sqrt,      "->BigInt"},
    {"bit_and",   (AngaraMethodFn)Angara_BigInt_bit_and,   "BigInt->BigInt"},
    {"bit_or",    (AngaraMethodFn)Angara_BigInt_bit_or,    "BigInt->BigInt"},
    {"bit_xor",   (AngaraMethodFn)Angara_BigInt_bit_xor,   "BigInt->BigInt"},
    {"shift_left",  (AngaraMethodFn)Angara_BigInt_shift_left,  "i->BigInt"},
    {"shift_right", (AngaraMethodFn)Angara_BigInt_shift_right, "i->BigInt"},
    {"sign",      (AngaraMethodFn)Angara_BigInt_sign,      "->i"},
    {"to_string", (AngaraMethodFn)Angara_BigInt_to_string, "->s"},
    {"to_i64",    (AngaraMethodFn)Angara_BigInt_to_i64,    "->i"},
    {"opAdd",     (AngaraMethodFn)Angara_BigInt_opAdd,     "BigInt->BigInt"},
    {"opSub",     (AngaraMethodFn)Angara_BigInt_opSub,     "BigInt->BigInt"},
    {"opMul",     (AngaraMethodFn)Angara_BigInt_opMul,     "BigInt->BigInt"},
    {"opDiv",     (AngaraMethodFn)Angara_BigInt_opDiv,     "BigInt->BigInt"},
    {"opRem",     (AngaraMethodFn)Angara_BigInt_opRem,     "BigInt->BigInt"},
    {"opNeg",     (AngaraMethodFn)Angara_BigInt_opNeg,     "->BigInt"},
    {"opEquals",  (AngaraMethodFn)Angara_BigInt_opEquals,  "BigInt->b"},
    {"opCmp",     (AngaraMethodFn)Angara_BigInt_opCmp,     "BigInt->i"},
    {NULL, NULL, NULL}
};

static const AngaraClassDef BIGINT_CLASS = { "BigInt", NULL, BIGINT_METHODS };

static const AngaraFuncDef BIGINT_EXPORTS[] = {
    {"from_i64",    Angara_bigint_from_i64,    "i->BigInt",    &BIGINT_CLASS},
    {"from_string", Angara_bigint_from_string, "s->BigInt",    &BIGINT_CLASS},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(bigint) {
    ang_api = api;
    *def_count = (sizeof(BIGINT_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return BIGINT_EXPORTS;
}
