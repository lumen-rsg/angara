# Unboxed Primitive Code Generation — Progress Tracker

## Steps

- [x] Step 1: Add infrastructure (`LocalKind` enum, `namedKinds` map, helper functions, type-aware `allocLocal` overload)
- [x] Step 2: Modify `loadVar` / `storeVar` for raw/boxed handling
- [x] Step 3: Modify `cgVarDecl` to use type-aware allocation
- [x] Step 4: Add `namedKinds` save/restore everywhere
- [x] Step 5: Fast arithmetic path in `cgBinary`
- [x] Step 6: Type-aware function parameters in `codegenFunctionDecl`
- [x] Step 7: Fix `cgForIn` counter to use raw `i64` alloca

## Benchmark Results

| Benchmark | Before | After | Change |
|---|---|---|---|
| Integer Loop (500M) | 46.16x | 2.37x | **19.5x faster** |
| Prime Sieve (1M) | 1.41x | 1.04x | near parity |
| Recursive Fibonacci (n=40) | 3.80x | 4.69x | slight regression |
| Matrix Multiply (200x200) | 2.25x | 1.05x | near parity |
| List Ops (1M items) | 1.00x | 1.05x | same |
| Bubble Sort (20K) | 1.94x | 0.86x | **beats C** |
| String Building (100K) | 1.14x | 1.11x | same |
| Data Class Churn (500K) | 6.00x | 2.82x | **2.1x faster** |

## Known Issues

- **Fib regression**: Raw allocas add box/unbox overhead for recursive functions
  where the function boundary is still objType. Fix requires typed function
  signatures (Phase 2: raw i64 params instead of objType).

## Verification Commands

```sh
make                  # build compiler
cd tests/lang && bash run_tests.sh   # regression tests
bash benchmarks/run.sh               # performance comparison
```

## Status

**Phase 1 complete.** All 7 steps implemented, 17/18 tests pass (1 pre-existing bug).
