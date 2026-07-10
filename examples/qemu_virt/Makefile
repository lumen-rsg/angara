# qemu_virt — Bare-metal Angara "Hello world+" for QEMU aarch64 (raw boot)
#
# Prerequisites (all commonly available on an aarch64 Linux host):
#   - angc (Angara compiler) in PATH
#   - clang (with the aarch64 target) — used as the cross assembler/linker
#   - ld.bfd (GNU linker) or any aarch64-aware linker
#   - llvm-objcopy
#   - qemu-system-aarch64
#
# Usage:
#   make          — Build kernel8.img
#   make run      — Build and boot in QEMU (Ctrl-A X to quit)
#   make verify   — Build, boot, and assert the banner appears (auto-quits)
#   make clean    — Remove build artifacts

# Prefer a locally-built compiler (../../build/angc) so the example tracks
# in-tree changes; fall back to an installed `angc` on PATH.
ROOT      := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
ANGC      := $(shell if [ -x "$(ROOT)/build/angc" ]; then echo "$(ROOT)/build/angc"; else echo "angc"; fi)
CC        = clang
LD        = ld.bfd
OBJCOPY   = llvm-objcopy
QEMU      = qemu-system-aarch64

# Cross-compile for the bare-metal aarch64 target.
CROSS_TARGET = aarch64-unknown-none-elf
ASFLAGS  = --target=$(CROSS_TARGET) -c -ffreestanding -nostdlib
LDFLAGS  = -T linker.ld -nostdlib -static

KERNEL   = kernel8.img

# QEMU virt: cortex-a57, 128 MiB RAM, PL011 UART on the serial console.
QEMU_OPTS = -machine virt -cpu cortex-a57 -m 128 -nographic -semihosting

BANNER = Angara bare-metal on QEMU virt

.PHONY: all run verify clean

all: $(KERNEL)

# Angara source → relocatable object (freestanding, no libc).
kernel.o: kernel.an
	$(ANGC) --freestanding --target aarch64 kernel.an

# Boot stub → object.
boot.o: boot.S
	$(CC) $(ASFLAGS) -o boot.o boot.S

# Link into a flat ELF at the QEMU load address.
kernel8.elf: boot.o kernel.o linker.ld
	$(LD) $(LDFLAGS) -o $@ boot.o kernel.o

# Raw binary image that QEMU's -kernel loader can consume.
$(KERNEL): kernel8.elf
	$(OBJCOPY) -O binary $< $@

# Boot interactively.
run: $(KERNEL)
	$(QEMU) $(QEMU_OPTS) -kernel $(KERNEL)

# Boot headlessly, capture output, and verify the banner printed.
# Times out after 5s (the kernel halts in an infinite loop, so QEMU never exits
# on its own). The grep makes this target succeed/fail deterministically.
verify: $(KERNEL)
	@timeout 5 $(QEMU) $(QEMU_OPTS) -kernel $(KERNEL) > qemu.out 2>&1 || true
	@if grep -q "$(BANNER)" qemu.out; then \
		echo "[OK] banner printed:"; \
		grep -A1 "$(BANNER)" qemu.out | head -2; \
	else \
		echo "[FAIL] banner not found in QEMU output:"; \
		cat qemu.out; \
		exit 1; \
	fi

clean:
	rm -f *.o *.elf *.img *.ll qemu.out
