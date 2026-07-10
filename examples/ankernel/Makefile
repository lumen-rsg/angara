# ankernel — Bare-metal Angara kernel for ARM64 (QEMU virt)
#
# Prerequisites:
#   - angc (Angara compiler) in PATH
#   - aarch64-unknown-elf toolchain (or aarch64-linux-gnu-)
#   - QEMU (qemu-system-aarch64)
#
# Usage:
#   make          — Build the kernel
#   make run      — Build and run in QEMU
#   make clean    — Remove build artifacts

CROSS = aarch64-unknown-elf-
AS      = $(CROSS)as
LD      = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy
ANGC = angc
QEMU = qemu-system-aarch64
# This minimal demo uses only peek/poke/halt — no atomics — so a Cortex-A57
# (pre-LSE) target is fine here. The sibling qemu_virt example exercises the
# atomic intrinsics and therefore requires -cpu max (ARMv8.1+ LSE).
ASFLAGS  = -mcpu=cortex-a57
LDFLAGS  = -T linker.ld -nostdlib -static
KERNEL = kernel8.img

.PHONY: all run clean

all: $(KERNEL)
ang_main.o: kernel.an
	$(ANGC) --freestanding --target aarch64 $<

boot.o: boot.S
	$(AS) $(ASFLAGS) -o boot.o boot.S

kernel8.elf: boot.o ang_main.o
	$(LD) $(LDFLAGS) -o $@ boot.o ang_main.o

$(KERNEL): kernel8.elf
	$(OBJCOPY) -O binary $< $@

run: $(KERNEL)
	$(QEMU) -machine virt -cpu cortex-a57 -m 128 \
		-nographic -semihosting \
		-kernel $(KERNEL)

debug: $(KERNEL)
	$(QEMU) -machine virt -cpu cortex-a57 -m 128 \
		-nographic -semihosting \
		-kernel $(KERNEL) \
		-S -gdb tcp::1234

clean:
	rm -f *.o *.elf *.img *.ll