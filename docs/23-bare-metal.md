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

### Interrupt-driven example

`examples/qemu_virt_irq/` is a step up: it configures the GICv2 interrupt
controller, the ARM generic physical timer, and an AArch64 exception vector
table, then prints a tick counter to UART on each timer interrupt. This bridges
the gap from "intrinsics exist" to "you can write a real interrupt-driven
bare-metal driver."

```sh
cd examples/qemu_virt_irq
make verify-irq   # boot in QEMU and assert TICK= lines appear
```

The vector table + register save/restore lives in `boot.S` (it's data at fixed
offsets); the GIC programming and handler logic is pure Angara (`kernel.an`).
See [`examples/qemu_virt_irq/README.md`](../examples/qemu_virt_irq/README.md)
for details.

## Intrinsic functions

The compiler provides built-in intrinsics for memory access, CPU control, atomics,
bit manipulation, memory barriers, interrupt control, cache maintenance, and
context switching. These do not require any header or library — declare them with
`intrinsic func` and the compiler lowers them directly to LLVM IR / machine
instructions. Intrinsics are recognized by name at the call site, so the
declaration's signature is what the type checker enforces.

> **Canonical source:** the tables below are generated from
> [`angc/includes/Intrinsics.h`](../angc/includes/Intrinsics.h), the single
> header shared by the compiler (codegen + coverage self-check), the LSP
> (hover + completion), and this documentation. Adding an intrinsic means adding
> one row there plus one codegen lowering arm — the compiler's self-check
> asserts they stay in sync.

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
LLVM atomic IR (`atomicrmw` / `cmpxchg`), which on AArch64 lowers to either
LSE instructions (`cas` / `ldadd` / `ldset` / `ldclr` / `ldeor` / `swp`) or
LL/SC retry loops (`ldxr` / `stxr`), depending on the target CPU's features.

> **LSE vs LL/SC — `--cpu` and `--target-features`.** By default the atomics
> lower for the **build host's** CPU: if the host has LSE (ARMv8.1+, e.g. Apple
> Silicon, `-cpu max`) you get single-instruction `cas`/`ldadd`; if not, you get
> `ldxr`/`stxr` loops. When cross-compiling for a pre-8.1 target (Cortex-A53/
> -A57/-A72 — e.g. Raspberry Pi 3), pass `--cpu cortex-a53` or
> `--target-features -lse` so the atomics lower to LL/SC and don't trap:
>
> ```sh
> angc kernel.an --freestanding --target aarch64 --cpu cortex-a53
> # or equivalently:
> angc kernel.an --freestanding --target aarch64 --target-features -lse
> ```
>
> No outline-atomics (`__aarch64_cas*` IFUNC helpers) are ever emitted — Angara
> drives the LLVM backend directly (not the clang driver), so there is no
> runtime library dependency either way.

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

#### TLB management (`@unsafe`)

Invalidate TLB entries once the MMU is enabled. These are normal privileged
system operations and only require `@unsafe`. The `is`-suffixed variants are
inner-shareable (they broadcast to other cores); the others apply to the local
core only.

| Intrinsic | Signature | Lowers to | Scope |
|-----------|-----------|-----------|-------|
| `tlbi_vmalle1` | `() -> nil` | `tlbi vmalle1` | all entries (local) |
| `tlbi_vmalle1is` | `() -> nil` | `tlbi vmalle1is` | all entries (inner-shareable) |
| `tlbi_alle1` | `() -> nil` | `tlbi alle1` | current-ASID entries (local) |
| `tlbi_alle1is` | `() -> nil` | `tlbi alle1is` | current-ASID entries (IS) |
| `tlbi_vae1` | `(addr as i64) -> nil` | `tlbi vae1, x0` | by VA, ASID-specific (local) |
| `tlbi_vae1is` | `(addr as i64) -> nil` | `tlbi vae1is, x0` | by VA, ASID-specific (IS) |
| `tlbi_vaae1` | `(addr as i64) -> nil` | `tlbi vaae1, x0` | by VA, ASID-agnostic (local) |
| `tlbi_vaae1is` | `(addr as i64) -> nil` | `tlbi vaae1is, x0` | by VA, ASID-agnostic (IS) |
| `tlbi_aside1` | `(asid as i64) -> nil` | `tlbi aside1, x0` | by ASID (local) |
| `tlbi_aside1is` | `(asid as i64) -> nil` | `tlbi aside1is, x0` | by ASID (IS) |
| `tlbi_vale1` | `(addr as i64) -> nil` | `tlbi vale1, x0` | last-level by VA (local) |
| `tlbi_vale1is` | `(addr as i64) -> nil` | `tlbi vale1is, x0` | last-level by VA (IS) |

#### MMU control registers (`@unsafe`)

Program the EL1 MMU. These are the register writes needed to enable paging:
set up the page-table base (`ttbr0`), memory attributes (`mair`), translation
control (`tcr`), and flip the M bit in the system control register (`sctlr`).
`get_sctlr` reads SCTLR_EL1 for the read-modify-write that enables the MMU.

| Intrinsic | Signature | Lowers to |
|-----------|-----------|-----------|
| `set_ttbr0` | `(addr as i64) -> nil` | `msr ttbr0_el1, x0` (translation table base 0) |
| `set_mair` | `(val as i64) -> nil` | `msr mair_el1, x0` (memory attribute indirection) |
| `set_tcr` | `(val as i64) -> nil` | `msr tcr_el1, x0` (translation control) |
| `set_sctlr` | `(val as i64) -> nil` | `msr sctlr_el1, x0` (system control; bit 0 = MMU on) |
| `get_sctlr` | `() -> i64` | `mrs sctlr_el1` (read system control) |

#### Privilege switching (`@privileged`)

The exception-return primitives. `eret` jumps to `elr_el1` with the `spsr_el1`
pstate, so an incorrect value is an unrecoverable fault. These require the
**`@privileged`** block (which implies `@unsafe`); calling them outside it is
**E925**. They belong in a boot stub or privilege-management layer.

| Intrinsic | Signature | Lowers to |
|-----------|-----------|-----------|
| `eret` | `() -> nil` | `eret` (exception return — terminal; never returns) |
| `set_spsr` | `(daif as i64) -> nil` | `msr spsr_el1, x0` (set the saved pstate for the next `eret`) |
| `set_elr` | `(addr as i64) -> nil` | `msr elr_el1, x0` (set the exception link register for the next `eret`) |

```angara
@privileged {
    set_spsr(0x3C5);       // EL1, IRQ unmasked, etc.
    set_elr(0x80000000);   // where to resume
    eret();                // never returns
}
```

The `wfi`/`wfe`/`sev`/`dmb`/`dsb`/`isb`/`dmb_st`/`dsb_st` intrinsics, the
`mrs`/`msr`-based DAIF/cache/register operations, and the `mov`/`msr` context
intrinsics are all AArch64 instructions; they assemble correctly when targeting
AArch64 and will fail at assembly time on other architectures (the intended
outcome — they're bare-metal operations). The atomics and bit operations lower
to LLVM IR that is portable across targets (LLVM picks the right lowering per
architecture); `dc_csw` mixes both — its arithmetic encoding is portable LLVM
IR, but the final `dc cisw` and the `csselr`/`ccsidr` accesses are AArch64.

#### RISC-V intrinsics (Tier 1)

Compile with `--target riscv64-unknown-none-elf` to target RISC-V bare metal.
The following intrinsics are RISC-V-specific (calling them on an AArch64 or x86
target is E926). The portable intrinsics (`halt`, `nop`, `atomic_*`, `clz`,
`ctz`, `rev`, `rbit`) work on RISC-V without change — they lower to LLVM IR.

`wfi` and `get_sp` are **multi-arch**: they are valid on both AArch64 and
RISC-V (the codegen picks the right instruction per target).

| Intrinsic | Signature | Lowers to |
|-----------|-----------|-----------|
| `rdcycle` | `() -> i64` | `rdcycle` (cycle counter) |
| `rdtime` | `() -> i64` | `rdtime` (real-time clock) |
| `rdinstret` | `() -> i64` | `rdinstret` (retired-instruction count) |
| `csrr` | `(csr as i64) -> i64` | `csrrw $0, $1, x0` (read CSR by number) |
| `csrw` | `(csr as i64, val as i64) -> nil` | `csrrw x0, $0, $1` (write CSR by number) |
| `fence` | `() -> nil` | `fence rw, rw` (full memory barrier) |
| `fence_i` | `() -> nil` | `fence.i` (instruction-cache sync) |
| `sfence_vma` | `() -> nil` | `sfence.vma zero, zero` (flush all TLB entries) |
| `wfi` | `() -> nil` | `wfi` (also available on AArch64) |
| `get_sp` | `() -> i64` | `mv $0, sp` (also available on AArch64 as `mov $0, sp`) |

The `csrr`/`csrw` intrinsics take a CSR *number* (e.g. `0xc00` for `mcycle`,
`0x300` for `mstatus`). The underlying `csrrw` instruction uses a GPR for the
CSR address so the number can be a runtime value.

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

> **Target note for atomics:** the atomic intrinsics lower to either AArch64
> **LSE** instructions (`cas`, `ldadd`, …) or **LL/SC** loops (`ldxr`/`stxr`),
> depending on the target. Pass `--cpu cortex-a53` or `--target-features -lse`
> for pre-8.1 cores (Cortex-A53/-A57/-A72); omit the flag for LSE-capable targets.
> See the LSE vs LL/SC note above.

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
| E925 | A privilege-transition intrinsic (`eret`/`set_spsr`/`set_elr`) outside a `@privileged` block. |
| E926 | An AArch64-only intrinsic (e.g. `get_el`, `wfi`, `dc_ivac`) called while targeting a non-AArch64 architecture. Portable intrinsics (`halt`, `nop`, `atomic_*`, `clz`, `ctz`, `rev`, `rbit`) are never gated. |

### `@privileged` — the privilege-transition escape hatch

`@unsafe` is the standard opt-out from the type system and borrow checker. The
privilege-transition intrinsics (`eret`, `set_spsr`, `set_elr`) need a stronger
gate: `eret` jumps to `elr_el1` with the `spsr_el1` pstate, so a wrong value is
an unrecoverable fault. They require **`@privileged { ... }`**, which implies
`@unsafe` (so inline asm inside needs no nested `@unsafe` wrapper). Calling them
outside `@privileged` is E925. Reserve `@privileged` for a boot stub or
privilege-management layer.

## Freestanding vs kernel mode

Angara has two strict compile modes that target non-hosted environments. They
are **mutually exclusive** (`--freestanding` and `--kernel` cannot be combined).
Both disable the hosted runtime and emit relocatable objects, but they differ
in *how much* of the Angara runtime they keep:

| Feature | `--freestanding` | `--kernel` |
|---------|------------------|-----------|
| **Purpose** | Bare metal, firmware, OS bring-up (no OS at all) | Linux loadable kernel module (`.ko`) |
| **Heap: strings, lists, records** | Banned (E915–E917). Use `[u8; N]` byte-array consts. | Full support — routes through the kernel allocator (`kmalloc`/`kfree` via a libc shim) |
| **Entry point** | `_start` (void, external linkage) — a boot stub branches to it | None — exported functions only; the module's `init_module` is C-side |
| **Threading** (`spawn`, `Mutex`) | Banned (E912/E913) | Banned (E902/E903) |
| **Exceptions** (`throw`, `try`/`catch`) | Banned (E910/E911) | Banned (E900/E901) |
| **Native module** `attach` | Banned (E914) | Banned (E904) |
| **IO** | None — drive MMIO via `peek`/`poke` | `printk` via `angara_kernel_print*` shim |
| **Allocator** | None (removed — see the "What works without libc" section) | Swappable vtable (`__ang_allocator_set`) → `kmalloc` |
| **Intrinsics** | Full set (MMIO, atomics, CPU control, cache, TLB, etc.) | Full set |
| **Inline assembly** | `@unsafe` blocks | `@unsafe` blocks |
| **Object symbol** | `_start` present, no `main` | Neither `_start` nor `main` |

**When to use which:**
- `--freestanding` for raw boot on QEMU or real hardware (the `qemu_virt` and
  `qemu_virt_irq` examples, Arduino, board bring-up).
- `--kernel` for Angara code compiled into a Linux kernel module — see
  [`KERNEL-MODE-IMPLEMENTATION.md`](./KERNEL-MODE-IMPLEMENTATION.md) for the
  full Linux driver guide, libc shim, and Kbuild bridge.

See also: the [`tests/kernel/`](../tests/kernel/) suite runs both modes
(gate tests `gate_*` → kernel E900–E904; `fs_gate_*` → freestanding E910–E917).

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
| E915 | strings (literals, concat, interpolation, `len`/`typeof`/`string`) | require heap allocation + the string runtime |
| E916 | lists (literals, `list<T>`, list methods) | require heap allocation + the list runtime |
| E917 | records, class instances, data instances | require heap allocation + the record runtime |

Use return codes or `match` instead of exceptions; write spinlock primitives in
inline asm instead of `Mutex`; declare hardware dependencies via
`foreign func` instead of `attach`; use fixed-size `i8` buffers with `peek`/`poke`
instead of strings/lists/records.

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
- **No-heap byte-array constants** — a top-level `const` annotated as
  `[u8; N]` (or `u8[N]`) and initialized with a `b"..."` byte-string literal
  lowers to a read-only `.rodata` global. No heap, no string runtime:

  ```angara
  const MSG as [u8; 20] = b"Hello from Angara!\r\n";

  func main() -> nil {
      for (let i = 0; i < len(MSG); i = i + 1) {
          poke32(0x09000000, MSG[i] as i64);  // write each byte to UART
      }
      halt();
  }
  ```

  `len(MSG)` folds to the compile-time size `N`. Subscript indexing (`MSG[i]`)
  returns a `u8` with a trap-on-out-of-bounds check, like raw arrays. Regular
  `"..."` string literals remain banned (E915) — use `b"..."` with a `[u8; N]`
  target instead.

What is **not** available:

- Heap string operations (`"..."` literals, concat, interpolation) — require
  heap allocation (E915). Use a `[u8; N]` byte-array const (above) instead.
- Lists — require heap allocation + the list runtime (E916).
- Records, class instances, data instances — require heap allocation (E917).
- Native module imports (`attach` of `.so`/`.dll`) — no dynamic loader (E914).
- Exception handling (`try`/`catch`/`throw`) — E910/E911.
- Threading (`spawn`, `Mutex`) — E912/E913.

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
