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
`CurrentEL` system register via inline assembly, and halts:

```sh
cd examples/qemu_virt
make            # build kernel8.img
make verify     # boot in QEMU and assert the banner printed
```

Expected serial output:

```
Angara bare-metal on QEMU virt (AArch64)
EL=1
```

See [`examples/qemu_virt/README.md`](../examples/qemu_virt/README.md) for the
full raw-boot flow and prerequisites.

## Intrinsic functions

The compiler provides built-in intrinsics for memory access, CPU control, and
memory barriers. These do not require any header or library — declare them with
`intrinsic func` and the compiler lowers them directly to LLVM IR / machine
instructions. Intrinsics are recognized by name at the call site, so the
declaration's signature is what the type checker enforces.

| Intrinsic | Signature | Lowers to |
|-----------|-----------|-----------|
| `peek8` / `peek16` / `peek32` / `peek64` | `(addr as i64) -> i64` | volatile load of N bits, zero-extended |
| `poke8` / `poke16` / `poke32` / `poke64` | `(addr as i64, val as i64) -> nil` | volatile store of N bits (truncated) |
| `halt` | `() -> nil` | `llvm.trap` + infinite loop |
| `nop` | `() -> nil` | `llvm.donothing` |
| `wfi` | `() -> nil` | `wfi` (wait for interrupt) |
| `wfe` | `() -> nil` | `wfe` (wait for event) |
| `sev` | `() -> nil` | `sev` (send event) |
| `dmb` | `() -> nil` | `dmb sy` (data memory barrier) |
| `dsb` | `() -> nil` | `dsb sy` (data synchronization barrier) |
| `isb` | `() -> nil` | `isb` (instruction synchronization barrier) |

The `wfi`/`wfe`/`sev`/`dmb`/`dsb`/`isb` intrinsics are AArch64 instructions;
they assemble correctly when targeting AArch64 and will fail at assembly time on
other architectures (the intended outcome — they're bare-metal operations).

```angara
intrinsic func peek32(addr as i64) -> i64;
intrinsic func poke32(addr as i64, val as i64) -> nil;
intrinsic func halt() -> nil;
intrinsic func dmb() -> nil;
intrinsic func isb() -> nil;
```

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
