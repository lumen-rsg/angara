# Unboxed Primitive Code Generation — Progress Tracker

## Steps

- [x] Step 1: Add infrastructure (`LocalKind` enum, `namedKinds` map, helper functions, type-aware `allocLocal` overload)
- [x] Step 2: Modify `loadVar` / `storeVar` for raw/boxed handling
- [x] Step 3: Modify `cgVarDecl` to use type-aware allocation
- [x] Step 4: Add `namedKinds` save/restore everywhere
- [x] Step 5: Fast arithmetic path in `cgBinary`
- [x] Step 6: Type-aware function parameters in `codegenFunctionDecl`
- [x] Step 7: Fix `cgForIn` counter to use raw `i64` alloca

## Phase 2: Typed Function Signatures

- [x] Step 8: `RawFuncInfo` struct, `m_raw_functions` map, `m_current_raw_return_kind`
- [x] Step 9: Generate raw LLVM signatures for all-unboxable functions
- [x] Step 10: Marshal args/returns in `callModuleFn` for raw callees
- [x] Step 11: Handle raw returns in `cgReturn`
- [x] Step 12: Implicit return default value for raw functions

## Benchmark Results

| Benchmark | Before | Phase 1 | Phase 2 | Change (P1→P2) |
|---|---|---|---|---|
| Integer Loop (500M) | 46.16x | 2.37x | **1.11x** | 2.1x faster |
| Prime Sieve (1M) | 1.41x | 1.04x | **0.88x** | beats C |
| Recursive Fibonacci (n=40) | 3.80x | 4.69x | **4.57x** | slight improvement |
| Matrix Multiply (200x200) | 2.25x | 1.05x | **1.00x** | parity with C |
| List Ops (1M items) | 1.00x | 1.05x | **1.05x** | same |
| Bubble Sort (20K) | 1.94x | 0.86x | **0.85x** | beats C |
| String Building (100K) | 1.14x | 1.11x | **1.16x** | same |
| Data Class Churn (500K) | 6.00x | 2.82x | **2.88x** | same |

## Known Issues

- **Fib regression (4.57x)**: Raw function signatures eliminate box/unbox at call boundaries,
  but the function body still computes with boxed values (`cg()` returns `objType`).
  Fixing this would require tracking raw context through expressions (Phase 3: raw expression eval).

## Build Note

**Important**: When modifying `LLVMBackend.h`, a `make clean` is required before rebuilding.
The Makefile does not track header dependencies for incremental builds.

## Verification Commands

```sh
make clean && make             # full build (required after header changes)
cd tests/lang && bash run_tests.sh   # regression tests (17/18 pass)
bash benchmarks/run.sh               # performance comparison
```

## Status

**Phase 2 complete.** All 12 steps implemented, 17/18 tests pass (1 pre-existing bug in 07_ffi).
