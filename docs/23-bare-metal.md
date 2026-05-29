# Bare-Metal Programming

Using Angara for OS and kernel development on freestanding targets.

---

## Overview

Angara can target freestanding environments with no libc dependency, making it suitable for OS and kernel development. Compile with `--freestanding` to disable libc and standard library linking:

```sh
angc kernel.an --freestanding --target aarch64-unknown-none-elf
```

## Intrinsic Functions

The compiler provides built-in intrinsics for memory access and CPU control. These do not require any header or library:

```angara
intrinsic func peek32(addr as i64) -> i64;     // Read 32 bits from address
intrinsic func poke32(addr as i64, val as i64) -> nil;  // Write 32 bits to address
intrinsic func halt() -> nil;                    // Stop the CPU
```

Intrinsics are translated directly to LLVM IR and require no runtime support.

## Example: ARM64 Kernel

A minimal kernel for the QEMU `virt` machine (ARM64):

```angara
// Declare intrinsics (compiler-provided, no libc needed)
intrinsic func peek32(addr as i64) -> i64;
intrinsic func poke32(addr as i64, val as i64) -> nil;
intrinsic func halt() -> nil;

// Print a single character to PL011 UART (QEMU virt: 0x0900_0000)
func uart_putc(c as i64) -> nil {
    // UART_FR is at offset 0x18 = 0x09000018
    // Bit 5 (0x20) = TX FIFO full
    while ((peek32(0x09000018) & 0x20) != 0) {}
    // UART_DR is at base = 0x09000000
    poke32(0x09000000, c);
}

// Kernel entry point
func main() -> nil {
    // Print "Hello from Angara!"
    uart_putc(72);   // H
    uart_putc(101);  // e
    uart_putc(108);  // l
    uart_putc(108);  // l
    uart_putc(111);  // o
    uart_putc(32);   // (space)
    uart_putc(102);  // f
    uart_putc(114);  // r
    uart_putc(111);  // o
    uart_putc(109);  // m
    uart_putc(32);   // (space)
    uart_putc(65);   // A
    uart_putc(110);  // n
    uart_putc(103);  // g
    uart_putc(97);   // a
    uart_putc(114);  // r
    uart_putc(97);   // a
    uart_putc(33);   // !
    uart_putc(13);   // CR
    uart_putc(10);   // LF

    halt();
}
```

## Building a Bare-Metal Kernel

The example in `examples/ankernel/` includes everything needed:

```sh
cd examples/ankernel
make
make run      # Runs in QEMU
```

The build process:
1. `angc` compiles `kernel.an` to an object file with `--freestanding`
2. The linker script (`linker.ld`) places code at the correct address
3. `boot.S` provides the entry point that jumps to `main()`
4. QEMU loads and runs the resulting ELF binary

## What Works Without libc

In freestanding mode, you can use:
- All integer and floating-point arithmetic
- Bitwise operations
- Control flow (if, while, for)
- Functions and recursion
- Intrinsic functions (peek, poke, halt)
- Foreign function declarations (for writing to hardware registers)

What is not available:
- String operations (require heap allocation)
- Lists and records (require runtime)
- Module imports (require dynamic loading)
- Exception handling (require setjmp/longjmp)
