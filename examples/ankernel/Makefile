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

# Toolchain — adjust prefix if needed
CROSS = aarch64-unknown-elf-
# Alternative: CROSS = aarch64-linux-gnu-
AS      = $(CROSS)as
LD      = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy

# Angara compiler
ANGC = angc

# QEMU
QEMU = qemu-system-aarch64

# Build flags
ASFLAGS  = -mcpu=cortex-a57
LDFLAGS  = -T linker.ld -nostdlib -static

# Output
KERNEL = kernel8.img

.PHONY: all run clean

all: $(KERNEL)

# Step 1: Compile Angara kernel to LLVM IR, then to object file
# The --freestanding --target flags tell angc to emit bare-metal code
ang_main.o: kernel.an
	$(ANGC) --freestanding --target aarch64 $<

# Step 2: Assemble boot stub
boot.o: boot.S
	$(AS) $(ASFLAGS) -o boot.o boot.S

# Step 3: Link everything into an ELF binary
kernel8.elf: boot.o ang_main.o
	$(LD) $(LDFLAGS) -o $@ boot.o ang_main.o

# Step 4: Convert to raw binary (what QEMU expects)
$(KERNEL): kernel8.elf
	$(OBJCOPY) -O binary $< $@

# Run in QEMU (Mach-virt, Cortex-A57, 128MB RAM)
run: $(KERNEL)
	$(QEMU) -machine virt -cpu cortex-a57 -m 128 \
		-nographic -semihosting \
		-kernel $(KERNEL)

# Run with GDB support (port 1234)
debug: $(KERNEL)
	$(QEMU) -machine virt -cpu cortex-a57 -m 128 \
		-nographic -semihosting \
		-kernel $(KERNEL) \
		-S -gdb tcp::1234

clean:
	rm -f *.o *.elf *.img *.ll