# LLB — Angara Linux Loader/Bootloader for QEMU virt (AArch64).
#
# Prerequisites:
#   - angc (Angara compiler) in PATH (or at ../../build/angc)
#   - clang (with aarch64 target support)
#   - ld.bfd or lld (aarch64-aware linker)
#   - llvm-objcopy
#   - qemu-system-aarch64
#
# Required input files (place in this directory):
#   - Image          — Linux kernel Image (arm64, uncompressed)
#                       Build with:
#                         make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig
#                         make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- Image -j$$(nproc)
#                       (output: arch/arm64/boot/Image)
#   - qemu_virt.dtb  — Device tree blob for QEMU virt
#                       Generate with: make dtb
#
# Usage:
#   make              — Build kernel8.img (LLB + embedded kernel + DTB)
#   make dtb          — Generate qemu_virt.dtb from QEMU
#   make run          — Build and boot in QEMU
#   make check-image  — Verify Image is a valid arm64 Linux kernel
#   make clean        — Remove build artifacts

ROOT      := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
ANGC      := $(shell if [ -x "$(ROOT)/build/angc" ]; then echo "$(ROOT)/build/angc"; else echo "angc"; fi)
CC        = clang
LD        = ld.bfd
OBJCOPY   = llvm-objcopy
QEMU      = qemu-system-aarch64

# Cross-compile for bare-metal aarch64
CROSS_TARGET = aarch64-unknown-none-elf
ASFLAGS  = --target=$(CROSS_TARGET) -c -ffreestanding -nostdlib
LDFLAGS  = -T linker.ld -nostdlib -static

KERNEL   = kernel8.img

# QEMU virt machine: 256 MiB RAM (room for LLB + kernel at 0x42000000 + DTB).
QEMU_OPTS = -machine virt -cpu max -m 256 -nographic

.PHONY: all dtb run clean check-image

all: $(KERNEL)

# ── Check for required input files ──
Image:
	@echo "[ERROR] Image not found in $$(pwd)"
	@echo ""
	@echo "Image is the raw arm64 Linux kernel image. Build one with:"
	@echo "  cd /path/to/linux"
	@echo "  make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig"
	@echo "  make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- Image -j\$$(nproc)"
	@echo "  cp arch/arm64/boot/Image $(CURDIR)/Image"
	@echo ""
	@echo "Or download a pre-built arm64 kernel Image."
	@exit 1

qemu_virt.dtb:
	@echo "[ERROR] qemu_virt.dtb not found. Run 'make dtb' to generate it."
	@exit 1

# ── Angara source → relocatable object ──
kernel.o: kernel.an
	$(ANGC) --freestanding --target aarch64 kernel.an

# ── Boot stub + embedded blobs → object ──
# The assembler expects Image and qemu_virt.dtb in this directory for .incbin.
boot.o: boot.S Image qemu_virt.dtb
	$(CC) $(ASFLAGS) -o boot.o boot.S

# ── Link into a flat ELF ──
kernel8.elf: boot.o kernel.o linker.ld
	$(LD) $(LDFLAGS) -o $@ boot.o kernel.o

# ── Raw binary image that QEMU's -kernel loader can consume ──
$(KERNEL): kernel8.elf
	$(OBJCOPY) -O binary $< $@
	@ls -lh $@

# ── Generate DTB for QEMU virt ──
# Uses QEMU's dumpdtb feature to produce a device tree that matches the
# virt machine. The resulting DTB embeds the proper memory map, interrupt
# controller (GICv2/v3), UART (PL011), and other peripherals the kernel
# expects to find via device tree.
dtb:
	$(QEMU) -machine virt -cpu cortex-a53 -m 256 \
		-M dumpdtb=qemu_virt.dtb 2>/dev/null
	@echo "[OK] Generated qemu_virt.dtb"

# ── Verify the Image file is a valid arm64 Linux kernel ──
# The Linux kernel Image starts with an executable instruction (arm64 branch
# or NOP) at offset 0, and has the magic "ARM\x64" at offset 0x38.
check-image: Image
	@if [ ! -f Image ]; then \
		echo "[FAIL] Image file not found."; exit 1; \
	fi
	@size=$$(wc -c < Image); \
	if [ $$size -lt 64 ]; then \
		echo "[FAIL] Image is too small ($$size bytes)."; exit 1; \
	fi
	@magic=$$(od -A n -t x4 -j 56 -N 4 Image 2>/dev/null | tr -d ' '); \
	if [ "$$magic" = "644d5241" ] || [ "$$magic" = "41524d64" ]; then \
		echo "[OK]  Image looks like a valid arm64 Linux kernel ($$(ls -lh Image | awk '{print $$5}'))."; \
	else \
		echo "[WARN] Image does not have the ARM\\x64 magic number (got 0x$$magic)."; \
		echo "       Make sure it is a valid Linux arm64 kernel Image."; \
	fi

# ── Boot the LLB in QEMU ──
# Ctrl-A X to quit; or run `make run-log` to capture output to qemu.out.
run: $(KERNEL)
	$(QEMU) $(QEMU_OPTS) -kernel $(KERNEL)

# ── Boot and capture output to qemu.out ──
run-log: $(KERNEL)
	@timeout 15 $(QEMU) $(QEMU_OPTS) -kernel $(KERNEL) > qemu.out 2>&1 || true
	@echo "[OK] Output saved to qemu.out ($$(wc -l < qemu.out) lines)"
	@head -20 qemu.out

# ── Clean build artifacts ──
clean:
	rm -f *.o *.elf *.img *.ll qemu.out
