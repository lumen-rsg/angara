# Unboxed Primitive Code Generation — Progress Tracker

## Phase 1: Unboxed Locals & Fast Arithmetic

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

## Phase 3: GC Overhead Elimination

- [x] Step 13: `functionNeedsGC` AST scanner (recursive walk for heap-allocating expressions)
- [x] Step 14: Skip `emitGcPushFrame`/`emitGcPopFrame` for primitive-only raw functions

## Benchmark Results

| Benchmark | Baseline | Phase 1 | Phase 2 | Phase 3 |
|---|---|---|---|---|
| Integer Loop (500M) | 46.16x | 2.37x | 1.11x | **1.00x** |
| Prime Sieve (1M) | 1.41x | 1.04x | 0.88x | **1.04x** |
| Recursive Fibonacci (n=40) | 3.80x | 4.69x | 4.57x | **0.97x** |
| Matrix Multiply (200x200) | 2.25x | 1.05x | 1.00x | **1.05x** |
| List Ops (1M items) | 1.00x | 1.05x | 1.05x | **1.00x** |
| Bubble Sort (20K) | 1.94x | 0.86x | 0.85x | **0.85x** |
| String Building (100K) | 1.14x | 1.11x | 1.16x | **1.05x** |
| Data Class Churn (500K) | 6.00x | 2.82x | 2.88x | **2.88x** |

## Key Insight (Phase 3)

The fib regression was caused by `emitGcPushFrame`/`emitGcPopFrame` calls on every function entry/exit.
These calls manipulate thread-local GC state and cannot be eliminated by LLVM's optimizer.
For primitive-only functions (no heap references), skipping the GC frame entirely eliminates ~3ns per call,
bringing fib from 4.57x to **0.97x** (beats C).

## Remaining Issue

- **Data Class Churn (2.88x)**: Objects use boxed `AngaraObject` representation by design.
  Each object allocation goes through `gc_alloc`. This is inherent to the boxed object model.

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

**Phase 3 complete.** All benchmarks at or below 1.05x vs C, except Data Class Churn (boxed objects).
17/18 tests pass (1 pre-existing bug in 07_ffi).
