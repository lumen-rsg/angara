# Unboxed Primitive Code Generation — Progress Tracker

## Steps

- [ ] Step 1: Add infrastructure (`LocalKind` enum, `namedKinds` map, helper functions, type-aware `allocLocal` overload)
- [ ] Step 2: Modify `loadVar` / `storeVar` for raw/boxed handling
- [ ] Step 3: Modify `cgVarDecl` to use type-aware allocation
- [ ] Step 4: Add `namedKinds` save/restore everywhere
- [ ] Step 5: Fast arithmetic path in `cgBinary`
- [ ] Step 6: Type-aware function parameters in `codegenFunctionDecl`
- [ ] Step 7: Fix `cgForIn` counter to use raw `i64` alloca

## Verification Commands

```sh
make                  # build compiler
bash tests/lang/run_tests.sh   # regression tests
bash benchmarks/run.sh         # performance comparison
```

## Status

**Not started** — plan approved, implementation pending.
