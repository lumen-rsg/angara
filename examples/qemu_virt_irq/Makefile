# qemu_virt_irq — Bare-metal interrupt demo for QEMU virt (AArch64).
#
# Configures the GIC + virtual timer, installs an exception vector table,
# and prints a tick counter on each timer interrupt.
#
# Prerequisites (same as qemu_virt):
#   - angc (Angara compiler) in PATH or built in-tree
#   - clang (with the aarch64 target)
#   - ld.bfd (GNU linker) or lld
#   - llvm-objcopy
#   - qemu-system-aarch64

ROOT      := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
ANGC      := $(shell if [ -x "$(ROOT)/build/angc" ]; then echo "$(ROOT)/build/angc"; else echo "angc"; fi)
CC        = clang
LD        = ld.bfd
OBJCOPY   = llvm-objcopy
QEMU      = qemu-system-aarch64

CROSS_TARGET = aarch64-unknown-none-elf
ASFLAGS  = --target=$(CROSS_TARGET) -c -ffreestanding -nostdlib
LDFLAGS  = -T linker.ld -nostdlib -static

KERNEL   = kernel8.img

QEMU_OPTS = -machine virt -cpu max -m 128 -nographic -semihosting

.PHONY: all run verify-irq clean

all: $(KERNEL)

# Angara source → relocatable object (freestanding, no libc).
kernel.o: kernel.an
	$(ANGC) --freestanding --target aarch64 kernel.an

# Boot stub + vector table → object.
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

# Boot headlessly, capture output, and verify the timer interrupt fired.
# The kernel prints "IRQ demo ready" on boot, then "TICK=<n>" ~10x/sec.
# We assert at least one TICK= line appears (proving the handler ran).
verify-irq: $(KERNEL)
	@timeout 5 $(QEMU) $(QEMU_OPTS) -kernel $(KERNEL) > qemu.out 2>&1 || true
	@if grep -q "IRQ demo ready" qemu.out && grep -q "TICK=" qemu.out; then \
		echo "[OK] boot banner + interrupt ticks:"; \
		grep "IRQ demo ready" qemu.out | head -1; \
		grep "TICK=" qemu.out | head -3; \
		ticks=$$(grep -c "TICK=" qemu.out); \
		echo "[OK] $$ticks tick(s) fired"; \
	else \
		echo "[FAIL] missing boot banner or TICK= line in QEMU output:"; \
		cat qemu.out; \
		exit 1; \
	fi

clean:
	rm -f boot.o kernel.o kernel8.elf $(KERNEL) qemu.out
