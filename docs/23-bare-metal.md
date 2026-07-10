# Bare-Metal Programming

Using Angara for OS, firmware, and kernel development on freestanding targets.

---

## Overview

Angara targets freestanding environments with no libc dependency, making it
suitable for OS and kernel development, firmware, and bare-metal board
bring-up. Compile with `--freestanding` to disable libc and standard-library
linking:

```sh
angc kernel.an --freestanding --target aarch64-unknown-none-elf
```

In freestanding mode the entry point is emitted as `_start` (with a `void`
return) instead of the hosted `main`, so a boot stub can branch to it directly.

## A verified, end-to-end example

`examples/qemu_virt/` is a "Hello world+" kernel that boots **raw** (no UEFI, no
firmware) on QEMU's aarch64 `virt` machine. It prints a banner, reads the
`CurrentEL` system register via inline assembly, and exercises the freestanding
intrinsic set — a monotonic-cas spinlock, the CPU/cycle-counter register reads,
the bit-manipulation ops, and the DAIF/cache control ops — then halts:

```sh
cd examples/qemu_virt
make                    # build kernel8.img
make verify             # boot in QEMU and assert the banner printed
make verify-intrinsics  # boot and assert the atomic/cycle/bit-op demo lines
make verify-tier3       # boot and assert the DAIF/stack demo lines
make verify-smp         # boot with -smp 2 and assert the banner still appears
```

Expected serial output (`make verify-intrinsics`):

```
Angara bare-metal on QEMU virt (AArch64)
EL=1
LOCK=1      # atomic_cas spinlock taken and held
CPU=0       # get_mpidr → CPU affinity
CYC=…       # cntpct → physical timer count
FRQ=3B9ACA00  # cntfrq → 1 GHz (QEMU -cpu max)
CLZ=2B      # clz(1<<20) = 43
RBIT=8000000000000000   # rbit(1) = bit 63
```

`make verify-tier3` additionally asserts the interrupt-control and
context-switch lines (values are CPU-state-dependent):

```
DAIF=…      # get_daif → saved mask word (round-tripped via set_daif)
SP=…        # get_sp → current stack pointer
```

See [`examples/qemu_virt/README.md`](../examples/qemu_virt/README.md) for the
full raw-boot flow and prerequisites.

## Intrinsic functions

The compiler provides built-in intrinsics for memory access, CPU control, atomics,
bit manipulation, memory barriers, interrupt control, cache maintenance, and
context switching. These do not require any header or library — declare them with
`intrinsic func` and the compiler lowers them directly to LLVM IR / machine
instructions. Intrinsics are recognized by name at the call site, so the
declaration's signature is what the type checker enforces.

#### Memory access (MMIO)

| Intrinsic | Signature | Lowers to |
|-----------|-----------|-----------|
| `peek8` / `peek16` / `peek32` / `peek64` | `(addr as i64) -> i64` | volatile load of N bits, zero-extended |
| `poke8` / `poke16` / `poke32` / `poke64` | `(addr as i64, val as i64) -> nil` | volatile store of N bits (truncated) |

#### CPU control & barriers

| Intrinsic | Signature | Lowers to |
|-----------|-----------|-----------|
| `halt` | `() -> nil` | `llvm.trap` + infinite loop |
| `nop` | `() -> nil` | `llvm.donothing` |
| `wfi` / `wfe` / `sev` | `() -> nil` | `wfi` / `wfe` / `sev` |
| `dmb` / `dsb` / `isb` | `() -> nil` | `dmb sy` / `dsb sy` / `isb` (full-system ordering) |
| `dmb_st` / `dsb_st` | `() -> nil` | `dmb ishst` / `dsb ishst` (store-store, inner-shareable; cheaper than `dmb sy` before a lock release) |

#### Atomic memory operations

Lock-free multi-core and DMA-buffer access. All use **monotonic** ordering (the
cheapest correct option); each `atomic_*` RMW returns the value previously at the
address, and `atomic_cas` returns the old value so callers can loop. Lowered to
LLVM atomic IR (`atomicrmw` / `cmpxchg`), which on AArch64 with LSE emits
`cas` / `ldadd` / `ldset` / `ldclr` / `ldeor` / `swp`.

| Intrinsic | Signature | Lowers to |
|-----------|-----------|-----------|
| `atomic_load` | `(addr as i64) -> i64` | `load ... monotonic` |
| `atomic_store` | `(addr as i64, val as i64) -> nil` | `store ... monotonic` |
| `atomic_cas` | `(addr as i64, expected as i64, desired as i64) -> i64` | `cmpxchg` (monotonic) — returns old value; `while (atomic_cas(p,0,1)!=0){}` is a spinlock |
| `atomic_add` / `atomic_sub` | `(addr as i64, val as i64) -> i64` | `atomicrmw add/sub` |
| `atomic_or` / `atomic_and` / `atomic_xor` | `(addr as i64, val as i64) -> i64` | `atomicrmw or/and/xor` |
| `atomic_xchg` | `(addr as i64, val as i64) -> i64` | `atomicrmw xchg` |

#### CPU register reads

| Intrinsic | Signature | Lowers to |
|-----------|-----------|-----------|
| `get_el` | `() -> i64` | `mrs CurrentEL; lsr #2` (exception level 0/1/2/3) |
| `get_mpidr` | `() -> i64` | `mrs MPIDR_EL1` (CPU affinity — tells cores apart) |
| `cntfrq` | `() -> i64` | `mrs CNTFRQ_EL0` (timer frequency) |
| `cntpct` | `() -> i64` | `mrs CNTPCT_EL0` (physical timer count; `cntpct`/`cntfrq` give a zero-glue cycle counter) |

#### Bit manipulation

Target-agnostic LLVM intrinsics — single instruction on AArch64. `clz`/`cttz`
are called with `zero_is_poison=false` so a zero input returns the width.

| Intrinsic | Signature | Lowers to |
|-----------|-----------|-----------|
| `clz` | `(x as i64) -> i64` | `llvm.ctlz.i64` → `clz` |
| `ctz` | `(x as i64) -> i64` | `llvm.cttz.i64` → `rbit`+`clz` |
| `rev` | `(x as i64) -> i64` | `llvm.bswap.i64` → `rev` (byte-swap) |
| `rbit` | `(x as i64) -> i64` | `llvm.bitreverse.i64` → `rbit` (bit-reverse) |

#### Interrupt control (DAIF)

The cheap critical-section primitive on arm64 — mask/unmask IRQ/FIQ directly
without an asm wrapper. `daifclr`/`daifset` take an immediate bit mask (`#1`=FIQ,
`#2`=IRQ, `#4`=SError, `#8`=Debug). `get_daif`/`set_daif` save and restore the
full mask word, so a critical section can nest correctly:

```angara
let saved as i64 = get_daif();
disable_irq();          // ... critical section, IRQs masked ...
set_daif(saved);        // restore the caller's mask exactly
```

| Intrinsic | Signature | Lowers to |
|-----------|-----------|-----------|
| `enable_irq` / `disable_irq` | `() -> nil` | `msr daifclr/daifset, #2` (unmask/mask IRQ) |
| `enable_fiq` / `disable_fiq` | `() -> nil` | `msr daifclr/daifset, #1` (unmask/mask FIQ) |
| `get_daif` | `() -> i64` | `mrs daif` (read the current mask word) |
| `set_daif` | `(daif as i64) -> nil` | `msr daif, x0` (restore a saved mask) |

#### Cache maintenance (DMA coherency)

The classic arm64 footgun: whenever a device DMAs into memory, the CPU's
D-cache lines may hold **stale** copies. Without explicit maintenance, the CPU
reads its own cached data instead of what the device just wrote. The rule:

- **Before reading device-written data:** `dc_ivac(addr)` — invalidate the
  D-cache line so the next load fetches from RAM.
- **Before a device reads CPU-written data:** `dc_cvac(addr)` (or `dc_civac`
  to also invalidate afterward) — push dirty lines out to RAM.
- **After writing new code:** `dc_civac(addr)` + `ic_ivau(addr)` — the
  I-cache must also be invalidated to the Point of Unification, or the CPU
  executes stale instructions.

Each op takes one virtual address in `$0` and acts on the **line containing
it**; loop over the whole buffer in line-size strides (read from `CTR_EL0`).

| Intrinsic | Signature | Lowers to |
|-----------|-----------|-----------|
| `dc_ivac` | `(addr as i64) -> nil` | `dc ivac, x0` (D-cache invalidate to PoC) |
| `dc_cvac` | `(addr as i64) -> nil` | `dc cvac, x0` (D-cache clean to PoC) |
| `dc_civac` | `(addr as i64) -> nil` | `dc civac, x0` (D-cache clean + invalidate) |
| `ic_ivau` | `(addr as i64) -> nil` | `ic ivau, x0` (I-cache invalidate to PoU) |
| `dc_csw` | `(set as i64, way as i64, level as i64) -> nil` | encode `(set,way)` from `CCSIDR_EL1` for `level`, then `dc cisw, x0` |

> **`dc_csw` note:** the arm64 `dc cisw` instruction takes a single
> register holding a pre-encoded `SetWay` value whose bit layout depends on
> cache geometry (line size, associativity, number of sets) for the selected
> level. `dc_csw` reads `CCSIDR_EL1` for `level` and encodes the operand
> itself — one call performs one encoded `dc cisw`. A full flush-all sweep is
> higher-level policy: iterate `level`/`set`/`way` in Angara and call
> `dc_csw` per combination.

#### Context switching & vector table

| Intrinsic | Signature | Lowers to |
|-----------|-----------|-----------|
| `get_sp` | `() -> i64` | `mov x0, sp` (current stack pointer; stack guards, context save) |
| `get_fp` | `() -> i64` | `mov x0, x29` (frame pointer; crash dumps, backtraces) |
| `ttbr0_el1` | `() -> i64` | `mrs ttbr0_el1` (translation table base — meaningful once an MMU is enabled) |
| `set_vbar` | `(addr as i64) -> nil` | `msr vbar_el1, x0` (one-shot boot call: point the exception vector at your table before enabling interrupts) |

The `wfi`/`wfe`/`sev`/`dmb`/`dsb`/`isb`/`dmb_st`/`dsb_st` intrinsics, the
`mrs`/`msr`-based DAIF/cache/register operations, and the `mov`/`msr` context
intrinsics are all AArch64 instructions; they assemble correctly when targeting
AArch64 and will fail at assembly time on other architectures (the intended
outcome — they're bare-metal operations). The atomics and bit operations lower
to LLVM IR that is portable across targets (LLVM picks the right lowering per
architecture); `dc_csw` mixes both — its arithmetic encoding is portable LLVM
IR, but the final `dc cisw` and the `csselr`/`ccsidr` accesses are AArch64.

```angara
intrinsic func peek32(addr as i64) -> i64;
intrinsic func poke32(addr as i64, val as i64) -> nil;
intrinsic func halt() -> nil;
intrinsic func dmb() -> nil;
intrinsic func isb() -> nil;

// Atomics — a monotonic-cas spinlock needs no hand-written asm:
intrinsic func atomic_cas(addr as i64, expected as i64, desired as i64) -> i64;
intrinsic func atomic_add(addr as i64, val as i64) -> i64;

// CPU id + a zero-glue cycle counter:
intrinsic func get_mpidr() -> i64;
intrinsic func cntpct() -> i64;
intrinsic func cntfrq() -> i64;

// Single-instruction bit ops (bitmap allocators, endianness shuffling):
intrinsic func clz(x as i64) -> i64;
intrinsic func rbit(x as i64) -> i64;

// Interrupt mask control (cheap critical sections, no asm wrapper):
intrinsic func disable_irq() -> nil;
intrinsic func enable_irq() -> nil;
intrinsic func get_daif() -> i64;
intrinsic func set_daif(daif as i64) -> nil;

// DMA coherency — invalidate before reading, clean before device reads:
intrinsic func dc_ivac(addr as i64) -> nil;
intrinsic func dc_civac(addr as i64) -> nil;
intrinsic func ic_ivau(addr as i64) -> nil;

// Exception-vector setup at boot, and stack/frame reads for context save:
intrinsic func set_vbar(addr as i64) -> nil;
intrinsic func get_sp() -> i64;
```

> **Target note for atomics:** the atomic intrinsics lower to AArch64 **LSE**
> instructions (`cas`, `ldadd`, …). Run the image on a CPU that advertises the
> LSE extension (ARMv8.1+); on older cores these trap as undefined. The
> `examples/qemu_virt` Makefile uses `-cpu max` for this reason.

## Inline assembly

For system-register access (`mrs`/`msr`), cache operations, or anything not
covered by an intrinsic, Angara provides inline assembly. Inline asm is lowered
directly to LLVM inline assembly and **requires an `@unsafe` block** (E920),
since it bypasses the type system and the borrow/escape analysis.

```angara
// Grammar:
//   asm( "template" (-> type)? (, (in|out|inout) "constraint" expr)* )
//
// Operands are positional ($0, $1, ...). Outputs are listed before inputs in
// the constraint string. An optional `-> type` gives the result type
// (defaults to nil); with one output operand the result register ($0) is read
// back as that type.

@unsafe {
    asm("nop");                                              // void, no operands
    asm("msr daifset, #3");                                  // mask IRQ/FIQ/Abt/Dbg

    // Read CurrentEL into the variable `el` (constraint "=r" = output register):
    let el as i64 = 0;
    el = asm("mrs $0, CurrentEL" -> i64, out("=r") el);

    // Mixed in/out: sum = a + b
    let a as i64 = 5;
    let b as i64 = 7;
    let sum as i64 = 0;
    asm("add $0, $1, $2", out("=r") sum, in("r") a, in("r") b);
}
```

Operand directions:
- `in("c") expr` — a read-only input value (integer-typed).
- `out("c") lvalue` — an output written to an assignable variable (a bare
  `let` variable).
- `inout("c") lvalue` — the variable is passed in and the result written back.

Constraints use the standard LLVM/GCC codes (`"r"` = any GPR, `"=r"` = output
GPR, `"=&r"` = early-clobber output, etc.). The template references operands by
position: `$0`, `$1`, …

### Error codes

| Code | Meaning |
|------|---------|
| E920 | `asm(...)` outside an `@unsafe` block. |
| E921 | The asm template is not a string literal. |
| E922 | An `out`/`inout` operand is not an assignable lvalue. |
| E923 | An asm operand is not integer-typed. |
| E924 | The `-> type` clause names a non-integer type. |

## Freestanding gates

`--freestanding` is a strict mode. Features that depend on the hosted runtime
are **hard errors at compile time**, not silent no-ops:

| Code | Rejected feature | Why |
|------|------------------|-----|
| E910 | `throw` | requires setjmp/longjmp/printf/exit |
| E911 | `try`/`catch` | requires setjmp/longjmp/printf/exit |
| E912 | `spawn()` | bare metal has no pthreads |
| E913 | `Mutex` | bare metal has no pthreads |
| E914 | `attach` of a native (`.so`/`.dylib`/`.dll`) module | bare metal has no dynamic loader |

Use return codes or `match` instead of exceptions; write spinlock primitives in
inline asm instead of `Mutex`; declare hardware dependencies via
`foreign func` instead of `attach`.

## What works without libc

In freestanding mode you can use:

- All integer and floating-point arithmetic (`i8`..`i64`, `u8`..`u64`, `f32`,
  `f64`, `bool`, `char`).
- Bitwise operations and shifts (with the usual division/modulo/shift-amount
  runtime checks routed to a trap, since there is nowhere to raise an exception).
- Control flow: `if`, `while`, `for`, `match`.
- Functions and recursion.
- All intrinsic functions (the table above).
- Inline assembly (in `@unsafe` blocks).
- `foreign func` declarations (resolved by your linker script / boot stub).

What is **not** available:

- String operations (require heap allocation).
- Lists and records (require the runtime).
- Native module imports (`attach` of `.so`/`.dll`).
- Exception handling (`try`/`catch`/`throw`).
- Threading (`spawn`, `Mutex`).

## Building a bare-metal kernel

The general process:

1. `angc` compiles `kernel.an` to a relocatable object with `--freestanding`.
2. A linker script (e.g. `linker.ld`) places code at the board's load address.
3. A small boot stub (`boot.S`) sets up the stack, clears BSS, and jumps to
   `_start`.
4. The linker produces an ELF; `objcopy` produces a raw image the board (or
   QEMU's `-kernel` loader) can consume.

### Verified toolchain

The `examples/qemu_virt/` Makefile builds with tools that are commonly available
on an aarch64 Linux host (no separate cross-toolchain install needed):

- **`clang --target=aarch64-unknown-none-elf`** as the cross assembler.
- **`ld.bfd`** (GNU `ld`; `lld` works too) as the linker.
- **`llvm-objcopy`** to produce the raw image.
- **`qemu-system-aarch64 -machine virt`** to boot.

If you have a bare-metal GNU cross-toolchain (`aarch64-unknown-elf-gcc` etc.),
swap the Makefile variables accordingly.
