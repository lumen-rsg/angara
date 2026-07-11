# qemu_virt_irq — Bare-metal interrupt demo for QEMU virt (AArch64)

A step up from [`qemu_virt`](../qemu_virt/): this kernel configures the GICv2
interrupt controller and the ARM generic physical timer, installs an AArch64
exception vector table, and prints a tick counter to UART on each timer
interrupt.

## What it demonstrates

- **GICv2 initialization** via MMIO (`peek`/`poke` to `0x08000000`/`0x08010000`)
- **Exception vector table** — the 16-slot AArch64 VBAR_EL1 layout
- **Physical timer interrupts** — CNTP_TVAL_EL0 / CNTP_CTL_EL0 programming
- **IRQ handler logic in pure Angara** — GIC acknowledge (IAR), dispatch,
  end-of-interrupt (EOIR)

## The split of labor

| Component | File | Why |
|-----------|------|-----|
| Vector table + register save/restore | `boot.S` | The vector table is *data* (16 entries × 0x80 bytes at fixed offsets). Inline asm can't lay out data at fixed addresses. The register save/restore needs precise stack-frame control. |
| GIC init, timer setup, handler logic | `kernel.an` | Pure Angara — MMIO via `peek`/`poke`, system registers via inline `asm`, IRQs via `enable_irq()` |

The vector table lives in the `.text` section (aligned to 2048 bytes) so
`objcopy -O binary` includes it in the raw image without truncation issues
from the NOBITS `.bss` section.

## Files

| File | Description |
|------|-------------|
| `kernel.an` | GIC init, timer init, `irq_handler()`, `main()` |
| `boot.S` | Boot stub + exception vector table + IRQ handler stub |
| `linker.ld` | Section layout (QEMU `-kernel` load address `0x40080000`) |
| `Makefile` | Build + `verify-irq` target |

## Prerequisites

Same as `qemu_virt`:
- `angc` (Angara compiler) in PATH or built in-tree
- `clang` (with the aarch64 target)
- `ld.bfd` (GNU linker)
- `llvm-objcopy`
- `qemu-system-aarch64`

## Build and verify

```sh
make             # build kernel8.img
make verify-irq  # boot in QEMU and assert TICK= lines appear
make run         # boot interactively
```

Expected serial output (`make verify-irq`):

```
IRQ demo ready
TICK=1
TICK=2
TICK=3
...
```

The timer fires ~10 times/sec (CNTFRQ / 10). The verify target asserts at
least one TICK= line appears, proving the interrupt fired and the handler ran.

## How it works

1. `boot.S` parks secondary CPUs, sets up the stack, clears BSS, installs
   the vector table via `msr vbar_el1`, and calls `_start`.
2. `main()` initializes the GIC (distributor + CPU interface), enables the
   physical timer IRQ (INTID 30), programs the timer for 10 Hz, and calls
   `enable_irq()`.
3. On each timer expiry, the CPU takes an IRQ exception, vectors to offset
   0x280 (CurrentEL SPx), branches to `el1_irq_handler` in `boot.S`.
4. The asm stub saves x0–x30, calls `irq_handler()` in Angara, restores, and
   returns via `eret`.
5. `irq_handler()` acknowledges the GIC (IAR), increments the counter,
   prints TICK=, reloads the timer, and signals end-of-interrupt (EOIR).
