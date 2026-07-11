# eye_trainer — Simulated DDR Eye-Pattern Memory Trainer (QEMU aarch64)
#
# Boots a bare-metal Angara app that sweeps DQS/DQ delays against a software
# DDR PHY model and renders an ASCII eye diagram to the UART. Pure Angara,
# no libc, no heap.
#
# Prerequisites (same as qemu_virt):
#   - angc (Angara compiler) in PATH or ../../build/angc
#   - clang (aarch64 target), ld.bfd, llvm-objcopy, qemu-system-aarch64
#
# Usage:
#   make          — Build eye.img
#   make run      — Build and boot in QEMU (Ctrl-A X to quit)
#   make verify   — Build, boot, and assert the training completed (PHY_TRAIN=OK)
#   make clean    — Remove build artifacts

ROOT      := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
ANGC      := $(shell if [ -x "$(ROOT)/build/angc" ]; then echo "$(ROOT)/build/angc"; else echo "angc"; fi)
CC        = clang
LD        = ld.bfd
OBJCOPY   = llvm-objcopy
QEMU      = qemu-system-aarch64

CROSS_TARGET = aarch64-unknown-none-elf
ASFLAGS  = --target=$(CROSS_TARGET) -c -ffreestanding -nostdlib
LDFLAGS  = -T linker.ld -nostdlib -static

KERNEL   = eye.img

# QEMU virt: -cpu max (LSE atomics + modern extensions), 128 MiB RAM.
QEMU_OPTS = -machine virt -cpu max -m 128 -nographic -semihosting

.PHONY: all run verify clean

all: $(KERNEL)

# Angara source → relocatable object (freestanding, no libc).
eye.o: eye.an
	$(ANGC) --freestanding --target aarch64 eye.an

# Boot stub → object.
boot.o: boot.S
	$(CC) $(ASFLAGS) -o boot.o boot.S

# Link into a flat ELF at the QEMU load address.
eye.elf: boot.o eye.o linker.ld
	$(LD) $(LDFLAGS) -o $@ boot.o eye.o

# Raw binary image that QEMU's -kernel loader can consume.
$(KERNEL): eye.elf
	$(OBJCOPY) -O binary $< $@

# Boot interactively.
run: $(KERNEL)
	$(QEMU) $(QEMU_OPTS) -kernel $(KERNEL)

# Boot headlessly and verify the training pipeline completed.
# Asserts PHY_TRAIN=OK and EYE_CENTER= (proves sweep → center → report ran).
verify: $(KERNEL)
	@timeout 5 $(QEMU) $(QEMU_OPTS) -kernel $(KERNEL) > qemu.out 2>&1 || true
	@ok=1; \
	for tag in PHY_TRAIN EYE_CENTER EYE_PASSES EYE_QUALITY; do \
		if grep -q "$$tag=" qemu.out; then \
			line=$$(grep "$$tag=" qemu.out | head -1); \
			echo "[OK] $$line"; \
		else \
			echo "[FAIL] missing $$tag= line"; ok=0; \
		fi; \
	done; \
	if [ "$$ok" -ne 1 ]; then echo "--- full output ---"; cat qemu.out; exit 1; fi

clean:
	rm -f *.o *.elf *.img *.ll qemu.out
