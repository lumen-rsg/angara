# ankernel — Bare-Metal Angara Kernel

A minimal bare-metal kernel written in **pure Angara**, targeting ARM64 (AArch64) and running on QEMU's `virt` machine.

## What is this?

This demonstrates Angara's `--freestanding` mode and the `intrinsic func` feature. The kernel boots on bare-metal ARM64, writes "Hello from Angara!" to the UART, and halts — with **zero C code** and **no standard library**.

## Angara Intrinsics

The kernel uses Angara's `intrinsic func` declarations — compiler-provided functions that generate inline LLVM IR for bare-metal memory access:

| Intrinsic | Signature | Description |
|---|---|---|
| `peek8` | `(addr: i64) -> i64` | Volatile byte read |
| `peek16` | `(addr: i64) -> i64` | Volatile halfword read |
| `peek32` | `(addr: i64) -> i64` | Volatile word read |
| `peek64` | `(addr: i64) -> i64` | Volatile doubleword read |
| `poke8` | `(addr: i64, val: i64) -> nil` | Volatile byte write |
| `poke16` | `(addr: i64, val: i64) -> nil` | Volatile halfword write |
| `poke32` | `(addr: i64, val: i64) -> nil` | Volatile word write |
| `poke64` | `(addr: i64, val: i64) -> nil` | Volatile doubleword write |
| `halt` | `() -> nil` | CPU trap + infinite loop |
| `nop` | `() -> nil` | No-operation |

## Prerequisites

1. **Angara compiler** (`angc`) — built and in your `PATH`
2. **ARM64 cross-toolchain** — e.g., `aarch64-unknown-elf-*` or `aarch64-linux-gnu-*`
   ```bash
   # macOS (Homebrew)
   brew install aarch64-elf-binutils

   # Ubuntu/Debian
   sudo apt install binutils-aarch64-linux-gnu
   ```
3. **QEMU** — `qemu-system-aarch64`
   ```bash
   # macOS
   brew install qemu

   # Ubuntu/Debian
   sudo apt install qemu-system-arm
   ```

## Build & Run

```bash
# Build the kernel image
make

# Run in QEMU (output to terminal)
make run

# Clean build artifacts
make clean
```

Expected output:
```
Hello from Angara!
Bare-metal ARM64 kernel running on QEMU virt.
```

Press `Ctrl-A X` to exit QEMU.

## Project Structure

| File | Description |
|---|---|
| `kernel.an` | Pure Angara kernel — UART driver + main |
| `boot.S` | ARM64 assembly boot stub (stack, BSS clear) |
| `linker.ld` | Linker script for QEMU virt memory layout |
| `Makefile` | Build orchestration |

## How It Works

1. **QEMU** loads `kernel8.img` at `0x40080000` and starts execution
2. **boot.S** sets up the stack, clears BSS, and calls `_start`
3. **Angara-generated `_start`** initializes the runtime and calls `main()`
4. **main()** writes to the PL011 UART MMIO registers using `peek32`/`poke32`
5. **halt()** triggers a CPU trap

## GDB Debugging

```bash
# Terminal 1: Start QEMU with GDB server
make debug

# Terminal 2: Connect with cross-GDB
aarch64-unknown-elf-gdb kernel8.elf
(gdb) target remote :1234
(gdb) break _entry
(gdb) continue