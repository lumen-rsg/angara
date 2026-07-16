# LLB — Angara Linux Loader/Bootloader

A bare-metal first-stage bootloader written in Angara that boots a stock
aarch64 Linux kernel on QEMU's `virt` machine — with **no firmware, no UEFI,
no U-Boot**.

The LLB is loaded by QEMU's `-kernel` mechanism at `0x40080000`. It copies an
embedded Linux kernel `Image` to a 2 MiB-aligned address, places an embedded
device tree blob (DTB), flushes caches, and jumps to the kernel following the
AArch64 Linux boot protocol.

## Quick start

```bash
# 1. Build a Linux kernel Image (or use a pre-built one)
cd /path/to/linux
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- Image -j$(nproc)
cp arch/arm64/boot/Image /path/to/angara/examples/llb/

# 2. Generate the DTB for QEMU virt
cd /path/to/angara/examples/llb
make dtb

# 3. Build and boot
make run
```

Expected serial output:

```
Angara LLB v0.1.0 — booting Linux on QEMU virt (AArch64)
EL=1
MMU=OFF
Kernel: 0x4008C000..0x41E8C000 (0x1E00000 bytes)
DTB:    0x41E8C000..0x41E98000 (0xC000 bytes)
Copying kernel to 0x42000000...
Copying DTB to 0x40000000...
Flushing caches...
Jumping to kernel...
[... Linux kernel boot messages ...]
```

## Requirements

- **angc** — Angara compiler (build from root with `make`, or installed in PATH)
- **clang** — with aarch64 target support (`--target=aarch64-unknown-none-elf`)
- **ld.bfd** — or `lld` (aarch64-aware linker)
- **llvm-objcopy** — for ELF → raw binary conversion
- **qemu-system-aarch64** — to boot the resulting image

## Files

| File | Purpose |
|------|---------|
| `boot.S` | Assembly stub: CPU setup, BSS clear, `.incbin` for kernel + DTB |
| `kernel.an` | Angara LLB: UART, memcpy, cache flush, Linux boot jump |
| `linker.ld` | Memory layout: LLB code → embedded kernel → embedded DTB → BSS |
| `Makefile` | Build, DTB generation, QEMU boot |
| `Image` | **(you provide)** Linux kernel Image (`arch/arm64/boot/Image`) |
| `qemu_virt.dtb` | **(generated)** Device tree for QEMU virt |

## Memory layout

```
0x40000000  ─┬─ RAM start (256 MiB)
             ├─ DTB destination (copied here at runtime)
0x40080000  ─├─ LLB: boot + Angara code + rodata
             ├─ Embedded kernel Image (~20–40 MiB, .incbin)
             ├─ Embedded DTB (~50 KiB, .incbin)
             ├─ BSS + stack
0x42000000  ─├─ Kernel destination (2 MiB aligned, copied at runtime)
0x50000000  ─┴─ RAM end
```

## How it works

1. **QEMU loads the flat binary** at `0x40080000` and jumps to `_entry` (in `boot.S`).
2. **boot.S** sets up the stack, clears BSS, parks secondary cores, and branches
   to Angara's `_start`.
3. **kernel.an** (`main` function):
   - Prints a banner via PL011 UART (MMIO at `0x09000000`)
   - Reads `CurrentEL` to display the exception level
   - Checks `SCTLR_EL1`; disables the MMU if it's on
   - Reads linker-embedded symbols (`_kernel_start`, `_dtb_start`, etc.)
   - Copies the kernel Image to `0x42000000` (2 MiB aligned)
   - Copies the DTB to `0x40000000`
   - Flushes D-cache and I-cache over the destination ranges
   - Sets registers per the AArch64 Linux boot protocol:
     - `x0` = physical address of DTB
     - `x1 = x2 = x3 = 0`
   - Branches to the kernel entry point (`br xN`)

## Custom kernel config

For a minimal kernel that boots quickly, try:

```bash
# In the Linux source tree:
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig
scripts/config -e VIRTUALIZATION -e KVM -d MODULES -d BLK_DEV_INITRD
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- Image -j$(nproc)
```

Add `console=ttyAMA0 earlycon=pl011,0x09000000` to the kernel command line in
the DTB if you want earlycon output (the LLB already prints via the PL011 UART
at `0x09000000`).

## Limitations

- **No initrd / rootfs support.** The LLB only passes the DTB; the kernel will
  panic when it can't find a root filesystem. This is expected — the LLB
  demonstrates the boot flow, not a full system.
- **Single-core.** Secondary cores are parked by `boot.S` in a `wfe` loop. The
  kernel can bring them up via PSCI if compiled with SMP support and the DTB
  includes the PSCI node (QEMU virt DTB does).
- **No kernel relocation.** The kernel is copied to a fixed `0x42000000`. If
  the kernel is configured with a non-zero `TEXT_OFFSET`, adjust `KERNEL_DST`
  in `kernel.an`.
- **QEMU virt only.** The LLB uses hard-coded MMIO addresses (`0x09000000` for
  UART) and assumes the QEMU virt memory map.

## Debugging

If the kernel doesn't boot, uncomment `-d in_asm,cpu_reset` in the QEMU flags
to trace execution:

```bash
qemu-system-aarch64 -machine virt -cpu max -m 256 -nographic \
    -kernel kernel8.img -d in_asm,cpu_reset 2>trace.log
```

Check that:
- The LLB banner appears (UART is working)
- The kernel Image size is non-zero
- The kernel was built for the correct architecture (`file Image` shows ARM aarch64)
