# Freestanding Intrinsic Expansion — Implementation Plan

> **Status: Tiers 1–4 implemented and verified.** The intrinsic set and the
> two-part implementation contract (declare `intrinsic func` + add a name-ladder
> arm) shipped in commit `7501bbf`; Tiers 1–4 (atomics, bit manipulation, DAIF/
> cache control, context switching) landed in `be0a6bd` and are covered by the
> `tests/kernel/` compile-contract tests and the `examples/qemu_virt` QEMU demo.
> This document now serves as the spec for the lowering patterns and as a
> roadmap for future tiers (see Tier 5 below).

## How intrinsics work in Angara (the contract)

An intrinsic is **declared** by the user and **recognized by name** at the call
site in codegen. No registration table — it is a ladder of `if (fn == "...")`
checks in one place.

1. **In source**, the user declares it (the declaration's signature is what the
   type checker enforces; the body is never emitted):
   ```angara
   intrinsic func atomic_cas(addr as i64, expected as i64, desired as i64) -> i64;
   ```
2. **In codegen**, add an arm to the name-ladder at
   `angc/backend/llvm/expr/ExprCodegen.cpp` (~line 1666–1740), inside the
   `VarExpr`-callee branch. Each arm builds LLVM IR and returns an AngaraObject
   box via `makeI64(...)` / `makeNil()` / `makeBool(...)`. Intrinsics are
   skipped in `codegenTopLevelDecls` (`TopLevel.cpp:21`) so no body is emitted.

Three lowering styles are already in use — follow the one that matches:
- **LLVM intrinsic** (`halt`→`llvm.trap`, `nop`→`llvm.donothing`,
  bit-manip→`llvm.ctlz` etc.) — preferred when one exists; target-agnostic.
- **Inline asm** (`wfi`, `dmb sy`, `get_el`) — for instructions with no LLVM
  intrinsic. Use the `emit_void_asm` lambda pattern already in place.
- **LLVM atomic IR** (`atomicrmw`, `cmpxchg`) — for the new atomics batch.

Helpers available in `LLVMBackend` (all member methods, accessed bare):
`getI64(boxed)` extracts the i64 payload; `makeI64(value)` / `makeNil()` /
`makeBool(b)` box a result; `builder->CreateIntToPtr(addr, ptrTy)` turns an i64
address into a pointer; `llvm::Type::getInt{8,16,32,64}Ty(*ctx)` for widths.

---

## Tier 1 — atomics + CPU registers (highest impact)

### Atomic memory operations
Single biggest gap. Without these, multi-core and lock-free DMA-buffer access
require hand-written asm for every CAS loop. Lower to LLVM atomic IR.

| Intrinsic | Signature | LLVM lowering | Ordering |
|-----------|-----------|---------------|----------|
| `atomic_load` | `(addr as i64) -> i64` | `CreateLoad(i64, ptr)` with `setAtomic(AtomicOrdering::Monotonic)` | monotonic |
| `atomic_store` | `(addr as i64, val as i64) -> nil` | `CreateStore` with `setAtomic(Monotonic)` | monotonic |
| `atomic_cas` | `(addr as i64, expected as i64, desired as i64) -> i64` | `CreateAtomicCmpXchg` | monotonic — returns the **old** value (not a bool) so callers can loop |
| `atomic_add` | `(addr as i64, val as i64) -> i64` | `CreateAtomicRMW(Add, ...)` | monotonic — returns old |
| `atomic_sub` | `(addr as i64, val as i64) -> i64` | `CreateAtomicRMW(Sub, ...)` | monotonic — returns old |
| `atomic_or`  | `(addr as i64, val as i64) -> i64` | `CreateAtomicRMW(Or, ...)` | monotonic — returns old |
| `atomic_and` | `(addr as i64, val as i64) -> i64` | `CreateAtomicRMW(And, ...)` | monotonic — returns old |
| `atomic_xor` | `(addr as i64, val as i64) -> i64` | `CreateAtomicRMW(Xor, ...)` | monotonic — returns old |
| `atomic_xchg`| `(addr as i64, val as i64) -> i64` | `CreateAtomicRMW(Xchg, ...)` | monotonic — returns old |

Notes:
- Use **monotonic** as the default ordering (cheapest correct option). Add
  `_acq`/`_rel`/`_acq_rel` suffixed variants only if a concrete need arises;
  don't speculatively build all orderings.
- `atomic_cas` returns the old value: `while (atomic_cas(lock_addr, 0, 1) != 0) {}`
  is a spinlock. Returning i64 (not bool) avoids a redundant compare.
- `CreateAtomicCmpXchg` returns a struct `{i64 old, i1 matched}`; extract the
  old value via `CreateExtractValue(call, 0)`.

### CPU register reads (siblings of the existing `get_el`)
```angara
intrinsic func get_mpidr() -> i64;   // mrs x0, MPIDR_EL1
intrinsic func cntfrq() -> i64;      // mrs x0, CNTFRQ_EL0  (timer frequency)
intrinsic func cntpct() -> i64;      // mrs x0, CNTPCT_EL0  (physical timer count)
```
Lower to inline asm with a single output register (`"=r"`), `hasSideEffects=false`
(they only read a register). `cntpct`+`cntfrq` give a zero-glue cycle counter;
`get_mpidr` lets Angara tell cores apart (today only `boot.S` reads it in asm).

---

## Tier 2 — bit/word manipulation + lighter barriers

### Bit manipulation (map to LLVM intrinsics — no asm, target-agnostic)
```angara
intrinsic func clz(x as i64) -> i64;   // llvm.ctlz.i64  → `clz`
intrinsic func ctz(x as i64) -> i64;   // llvm.cttz.i64  → `rbit`+`clz`
intrinsic func rev(x as i64) -> i64;   // llvm.bswap.i64 → `rev`
intrinsic func rbit(x as i64) -> i64;  // llvm.bitreverse.i64 → `rbit`
```
Lower via `llvm::Intrinsic::getOrInsertDeclaration(mod.get(), {llvm::ctlz, ...})`.
Pass `is_zero_poison=false` for ctlz/cttz so a zero input returns the width
(not poison). Single-instruction on arm64; pure win for bitmap allocators,
priority encoders, and DMA endianness shuffling.

### Lighter barrier variants
The existing `dmb`/`dsb` are hardcoded to `sy` (full-system, strongest). Most
critical sections only need the cheaper store-store variant:
```angara
intrinsic func dmb_st() -> nil;   // "dmb ishst" — store-store, inner-shareable
intrinsic func dsb_st() -> nil;   // "dsb ishst"
```
Use the existing `emit_void_asm` pattern. (`dmb ishst` is what you want before
releasing a lock; `dmb sy` is overkill there.)

---

## Tier 3 — interrupt/exception control + cache maintenance

Needed once you go past "Hello world+" into a real interrupt controller (GIC)
and DMA-coherent drivers. All lower to inline asm.

### DAIF (interrupt mask) control
```angara
intrinsic func enable_irq() -> nil;      // msr daifclr, #2
intrinsic func disable_irq() -> nil;     // msr daifset, #2
intrinsic func enable_fiq() -> nil;      // msr daifclr, #1
intrinsic func disable_fiq() -> nil;     // msr daifset, #1
intrinsic func get_daif() -> i64;        // mrs x0, daif
intrinsic func set_daif(daif as i64) -> nil;  // msr daif, x0
```
Enable/disable IRQ/FIQ is the cheap critical-section primitive (no asm wrapper).
`set_daif` is used to restore a saved DAIF when returning from a critical section.

### Cache maintenance (DMA coherency — the classic arm64 footgun)
```angara
intrinsic func dc_ivac(addr as i64) -> nil;    // "dc ivac, x0"  — invalidate to PoC
intrinsic func dc_civac(addr as i64) -> nil;   // "dc civac, x0" — clean+invalidate
intrinsic func dc_cvac(addr as i64) -> nil;    // "dc cvac, x0"  — clean to PoC
intrinsic func ic_ivau(addr as i64) -> nil;    // "ic ivau, x0"  — invalidate I-cache to PoU
intrinsic func dc_csw(set as i64, way as i64) -> nil;  // "dc isw, x0" — set/way maintenance
```
Required whenever a device DMAs into memory: invalidate stale D-cache lines
before reading device-written data, and clean lines before the device reads
CPU-written data. Without these, every DMA driver needs C/asm glue.

---

## Tier 4 — context switching / vector table (lower priority)

```angara
intrinsic func get_sp() -> i64;            // current stack pointer (stack guards, context save)
intrinsic func get_fp() -> i64;            // frame pointer
intrinsic func ttbr0_el1() -> i64;         // translation table base (if MMU enabled)
intrinsic func set_vbar(addr as i64) -> nil;  // "msr vbar_el1, x0" — set exception vector base
```
`set_vbar` is the one-shot boot call that points at your exception vector table
before enabling interrupts. The rest are for context switching / crash dumps.

---

## Implementation order (as executed)

Tiers 1–4 were implemented in this order, which worked well and is recommended
for any future re-derivation:

1. **Atomics (Tier 1)** — unlocks multi-core + lock-free drivers; most common
   reason people fall back to C. ~9 intrinsics, one new lowering style
   (`atomicrmw`/`cmpxchg`).
2. **`cntpct`/`cntfrq`/`get_mpidr` (Tier 1)** — cheap, immediately useful,
   same inline-asm-with-output pattern as `get_el`.
3. **`clz`/`ctz`/`rev`/`rbit` (Tier 2)** — LLVM intrinsics, no asm, pure win.
4. **`enable_irq`/`disable_irq` + `set_vbar` (Tier 3/4)** — when moving to
   real interrupts.

## Verification (done)

- **Compile-contract tests:** `tests/kernel/fs_intrinsics.an` declares and
  exercises every intrinsic across all four tiers under `--freestanding` and
  must emit a clean relocatable object with `_start`.
- **IR-lowering tests:** `tests/kernel/run_ir_tests.sh` compiles
  `fs_ir_probe.an` with `--emit-llvm` and asserts the expected LLVM IR token
  for each family (atomic ops → `atomicrmw`/`cmpxchg`; bit ops →
  `llvm.ctlz`/`bswap`/`bitreverse`; `halt` → `llvm.trap`; register reads → the
  matching `mrs`/`mov` inline asm). This catches lowering regressions without
  QEMU. Run via `bash tests/kernel/run_kernel_tests.sh`.
- **QEMU demo:** `examples/qemu_virt/` boots raw on `qemu-system-aarch64
  -machine virt -cpu max -smp 2` and exercises the intrinsics end-to-end
  (`make verify`, `verify-intrinsics`, `verify-tier3`, `verify-smp`).
- **Docs:** the intrinsic tables in `docs/23-bare-metal.md` list the shipped
  entries, grouped by category.

---

## Tier 5 — TLB management / privilege switching (future, not yet implemented)

These were previously listed as out-of-scope. They are promoted here as the
natural next tier once a freestanding program enables its MMU or crosses
exception levels — but they are sketched, not built. Each follows the existing
declare-then-name-ladder contract.

### TLB management
```angara
intrinsic func tlbi_vmalle1() -> nil;     // "tlbi vmalle1" — invalidate all, EL1
intrinsic func tlbi_vaae1(addr as i64) -> nil;  // "tlbi vaae1, x0" — by VA, ASID-agnostic
```
Only relevant once the MMU is on (a larger effort than intrinsics). Lower to
inline asm via the `emit_void_asm[_in]` helpers.

### EL-switching
```angara
intrinsic func eret() -> nil;             // "eret" — exception return
intrinsic func set_spsr(daif as i64) -> nil;   // "msr spsr_el1, x0" — set saved pstate
intrinsic func set_elr(addr as i64) -> nil;    // "msr elr_el1, x0" — set exception link
```
`eret`/`hvc`/`smc` are the privilege-transition instructions. These are sharp
tools: `eret` without a correctly initialised `spsr`/`elr` is an unrecoverable
fault, and `hvc`/`smc` trap to a hypervisor/secure monitor that may not exist.
They belong in a boot stub or privilege-management layer rather than general
driver code, so each should carry a prominent safety note in its gate message.

## Out of scope (intentionally not proposed)

- Floating-point intrinsics (freestanding already supports `f64` arithmetic;
  no `fenv`/rounding-mode control is worth adding until a concrete need exists).
