# qemu_virt — Bare-metal Angara on QEMU aarch64 (raw boot)

A "Hello world+" kernel written almost entirely in **Angara**, booting **raw**
(no UEFI, no firmware, no bootloader) on QEMU's `virt` machine. It demonstrates
the language's freestanding story end to end.

## What it does

On boot it prints:

```
Angara bare-metal on QEMU virt (AArch64)
EL=1
```

…then halts. Along the way it exercises:

- **MMIO** via `peek32`/`poke32` to the PL011 UART (`0x0900_0000`).
- **Inline assembly** — `asm("mrs $0, CurrentEL" -> i64, out("=r") el)` reads the
  current Exception Level system register, the way you'd reach any `MRS`/`MSR`
  that has no higher-level wrapper.
- **Barrier intrinsics** — `dmb()` / `isb()` / `dsb()` order the UART writes.
- A **no-heap hex formatter** — pure integer shifts/masks, no strings or lists
  (both require the runtime, which bare metal doesn't have).

The only non-Angara code is [`boot.S`](boot.S): ~20 lines of assembly that set up
the stack, zero the BSS, and branch to the Angara-generated `_start`. Those few
steps are genuinely unreachable from a high-level language (there is no stack
yet to run HLL code on).

## Files

| File | Purpose |
|------|---------|
| `kernel.an` | The kernel, in Angara. Entry point is `main()`, emitted as `_start`. |
| `boot.S` | ARM64 boot stub: park secondary CPUs, set sp, clear BSS, call `_start`. |
| `linker.ld` | Places everything at `0x40080000` (QEMU's `-kernel` load address). |
| `Makefile` | Builds `kernel8.img`, with `run` and `verify` targets. |

## Prerequisites

| Tool | Notes |
|------|-------|
| `angc` | The Angara compiler. The Makefile prefers a local `../../build/angc`, else PATH. |
| `clang` | With the `aarch64` target — used as the cross assembler. |
| `ld.bfd` | Any aarch64-aware linker works (`lld`, GNU `ld`). |
| `llvm-objcopy` | Produces the raw `kernel8.img`. |
| `qemu-system-aarch64` | The machine emulator. |

## Build & run

```sh
make            # build kernel8.img
make run        # boot in QEMU (Ctrl-A X to quit)
make verify     # boot headlessly and assert the banner printed
make clean
```

`make verify` boots QEMU with a 5-second timeout (the kernel halts in an
infinite loop, so QEMU never exits on its own) and greps the serial output for
the banner. It exits non-zero if the banner is missing.

## How raw boot works (no UEFI)

1. QEMU's `-kernel kernel8.img` loads the raw binary at `0x40080000` and jumps
   there with the CPU in EL1.
2. `boot.S` (`_entry`) parks all CPUs except CPU 0, sets the stack pointer to
   `_stack_top`, zeroes the BSS, and calls `_start`.
3. `_start` is Angara's `main()` — emitted with `void` return and `ExternalLinkage`
   in `--freestanding` mode. It runs the banner, reads `CurrentEL`, and `halt()`s.

No firmware, no device tree parsing, no UEFI runtime — just the CPU, the UART,
and Angara.
