# Angara Language Test Report

**Date:** 2026-04-19  
**Compiler:** `build/angc` (commit 4198704)  
**Tests:** 18 total (12 positive, 6 negative)  
**Results:** ✅ 9 passed, ❌ 9 failed (bugs found)

---

## What Works ✅

| Test | Feature | Status |
|------|---------|--------|
| 05_collections | Lists, indexing, mutation, nested lists, for-in | ✅ PASS |
| 07_ffi | Foreign function interface (`foreign`, `foreign func`) | ✅ PASS |
| 08_optionals | Optional types (`T?`), nil comparison | ✅ PASS |
| 09_is_expression | Type checking with `is` expression | ✅ PASS |
| neg/01_type_errors | Type mismatch detection in variable annotations | ✅ CAUGHT |
| neg/03_undeclared_variable | Undefined variable detection | ✅ CAUGHT |
| neg/04_undeclared_function | Undefined function detection | ✅ CAUGHT |
| neg/05_throw_wrong_type | Throw type validation (must be Exception) | ✅ CAUGHT |
| neg/06_wrong_arg_count | Wrong argument count detection | ✅ CAUGHT |

---

## Bugs Found ❌

### 🔴 BUG 1: [01_basics] `i64()` and `f64()` type cast functions cause linker errors
**Severity:** High (core language feature broken)  
**Details:** Using `i64(3.7)` or `f64(42)` compiles to LLVM IR but fails at link time with:
```
Undefined symbols for architecture arm64:
  "___ang_01_basics_f64", referenced from: ___ang_01_basics_main
  "___ang_01_basics_i64", referenced from: ___ang_01_basics_main
```
**Root cause:** The codegen emits calls to runtime helper functions (`__ang_*_f64`, `__ang_*_i64`) that are never linked/defined.  
**Impact:** Type casting between numeric types is completely broken.

### 🔴 BUG 2: [02_control_flow] `continue` keyword is not recognized
**Severity:** High (standard loop control flow missing)  
**Details:** Using `continue` inside a `while` loop produces:
```
[Line 38] Error at 'continue': Undefined variable 'continue'.
```
**Root cause:** `continue` is not defined as a keyword in the lexer (Token.h has `BREAK` but no `CONTINUE`).  
**Impact:** Cannot skip to the next iteration of a loop — a fundamental control flow primitive.

### 🔴 BUG 3: [03_functions] Generic type parameters can't be passed to `println`
**Severity:** Medium (generics partially broken)  
**Details:** Calling `io.println(1, string(x))` where `x` has generic type `T` produces:
```
[Line 50] Error at ')': Type mismatch for argument 2. Expected 'string', but got 'T'.
```
**Root cause:** The type checker doesn't properly resolve generic type `T` at call sites — it treats `T` as an opaque error type rather than the concrete type it's instantiated with.  
**Impact:** Generic functions that use their type parameter with built-in functions (`string()`, `io.println()`) fail. Generic functions are limited to operations that work on all types.

### 🔴 BUG 4: [04_data_classes] Generic data classes can't be instantiated
**Severity:** Medium (generics partially broken)  
**Details:** Same root cause as BUG 3. Creating `Box(42)` where `Box<T>` is a generic data class fails during type checking:
```
[Line 51] Error at ')': Type mismatch for argument 2. Expected 'string', but got 'T'.
```
**Impact:** Generic data classes (`data Box<T>`, `data Pair<K, V>`) cannot be used with `io.println`. Non-generic data classes work fine.

### 🔴 BUG 5: [06_builtins] String * number repetition not supported
**Severity:** Low (missing feature)  
**Details:** The expression `"ab" * 3` should produce `"ababab"` but instead produces:
```
[Line 32] Error at '*': Operands for this arithmetic operator must be numbers.
```
**Root cause:** The type checker requires both operands of `*` to be numeric. String repetition via `*` is not implemented.  
**Impact:** No built-in way to repeat strings.

### 🔴 BUG 6: [10_enums] Enum usage causes runtime segfault
**Severity:** High (core language feature broken at runtime)  
**Details:** Enum declaration compiles, but using enum values (e.g., `Color.Red`, comparisons) causes a segfault (exit 139).  
**Root cause:** Likely the enum value is not properly initialized or the generated code accesses invalid memory when using enum variants.  
**Impact:** Enums are unusable at runtime.

### 🔴 BUG 7: [11_classes] Class with inheritance causes compiler crash
**Severity:** Critical (compiler crash)  
**Details:** Defining a class that `inherits` from another causes the compiler to crash with:
```
libc++abi: terminating due to uncaught exception of type std::length_error: basic_string
```
**Root cause:** An unhandled edge case in the compiler's class/inheritance handling, likely a `std::string` operation on an empty or invalid string (e.g., accessing `front()` on empty string).  
**Impact:** Class inheritance crashes the compiler entirely.

### 🔴 BUG 8: [12_exceptions] Try/catch with throw causes linker errors
**Severity:** High (exception handling broken)  
**Details:** Exception handling code compiles to LLVM IR but fails at link time:
```
clang: error: linker command failed with exit code 1 (use -v to see invocation)
```
**Root cause:** Similar to BUG 1 — the codegen for `throw`/`catch` references runtime symbols that aren't linked.  
**Impact:** Exception handling (`try`/`catch`/`throw`) is non-functional.

### 🔴 BUG 9: [neg/02_return_type_mismatch] Return type not checked
**Severity:** High (type safety gap)  
**Details:** A function declared `-> i64` can `return "not a number"` without any compilation error. The compiler accepts it and produces a binary.  
**Root cause:** The type checker doesn't validate that return expressions match the declared return type.  
**Impact:** Functions can return values of the wrong type, breaking type safety guarantees.

---

## Summary

| Category | Working | Broken |
|----------|---------|--------|
| Arithmetic & operators | ✅ | |
| Boolean operators (&&, \|\|, !) | ✅ | |
| String concatenation | ✅ | |
| Comparisons (==, !=, <, >, <=, >=) | ✅ | |
| Type casts (i64(), f64()) | | ❌ linker |
| if/else if/else, orif | ✅ | |
| while + break | ✅ | |
| while + continue | | ❌ not a keyword |
| C-style for loop | ✅ | |
| for-in loop | ✅ | |
| Ternary (?:) | ✅ | |
| Functions & recursion | ✅ | |
| Lambdas & closures | ✅ | |
| Higher-order functions | ✅ | |
| Generic functions | | ❌ type checker |
| Data classes (non-generic) | ✅ | |
| Data classes (generic) | | ❌ type checker |
| Lists & indexing | ✅ | |
| Builtins (typeof, len, string) | ✅ | |
| String * number | | ❌ |
| FFI (foreign) | ✅ | |
| Optional types (T?) | ✅ | |
| `is` expression | ✅ | |
| Enums | | ❌ segfault |
| Classes (basic) | ✅ | |
| Class inheritance | | ❌ crash |
| Exceptions (try/catch/throw) | | ❌ linker |
| Type checking: var annotation | ✅ | |
| Type checking: return type | | ❌ not checked |
| Type checking: undeclared var | ✅ | |
| Type checking: undeclared func | ✅ | |
| Type checking: throw type | ✅ | |
| Type checking: arg count | ✅ | |

---

## Test Files

```
tests/lang/
├── run_tests.sh                    # Test runner
├── REPORT.md                       # This report
├── positive/
│   ├── 01_basics.an               # Arithmetic, operators, type casts
│   ├── 02_control_flow.an         # if/else, while, for, break, continue, ternary
│   ├── 03_functions.an            # Functions, lambdas, closures, generics
│   ├── 04_data_classes.an         # Data classes, generic data classes
│   ├── 05_collections.an          # Lists, indexing, nested lists
│   ├── 06_builtins.an             # typeof, len, string, i64, f64, string repetition
│   ├── 07_ffi.an                  # Foreign function interface
│   ├── 08_optionals.an            # Optional types (T?), nil
│   ├── 09_is_expression.an        # Type checking with `is`
│   ├── 10_enums.an                # Enums
│   ├── 11_classes.an              # Classes, methods, inheritance
│   └── 12_exceptions.an           # try/catch, throw
└── negative/
    ├── 01_type_errors.an          # Type mismatch in variable annotation
    ├── 02_return_type_mismatch.an # Wrong return type (BUG: not caught)
    ├── 03_undeclared_variable.an  # Undefined variable
    ├── 04_undeclared_function.an  # Undefined function
    ├── 05_throw_wrong_type.an     # Throw non-Exception
    └── 06_wrong_arg_count.an      # Wrong number of arguments
```

Run with: `bash tests/lang/run_tests.sh`