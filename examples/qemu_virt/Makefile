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
#   make                 — Build kernel8.img
#   make run             — Build and boot in QEMU (Ctrl-A X to quit)
#   make verify          — Build, boot, and assert the banner appears (auto-quits)
#   make verify-intrinsics — Boot and assert the Tier 1+2 intrinsic demo lines
#   make verify-tier3    — Boot and assert the Tier 3+4 (DAIF/cache/SP) demo lines
#   make verify-tier5    — Boot and assert the Tier 5 (TLB/SPSR/ELR/eret) demo lines
#   make verify-smp      — Boot with -smp 2 and assert the banner still appears
#   make clean           — Remove build artifacts

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

# QEMU virt: a modern ARMv8.x CPU (`-cpu max`) so LSE atomics (cas/ldadd/...)
# are available for the intrinsic demo, 128 MiB RAM, PL011 UART on the console.
# (The previous -cpu cortex-a57 predates the LSE extension, so cmpxchg-based
#  atomics trap; `max` advertises LSE and is the modern virt default.)
QEMU_OPTS = -machine virt -cpu max -m 128 -nographic -semihosting

BANNER = Angara bare-metal on QEMU virt

.PHONY: all run verify verify-intrinsics verify-tier3 verify-tier5 verify-smp clean

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

# Boot headlessly and verify the Tier 1 + 2 intrinsic demo lines printed:
#   LOCK=1  CPU=0  CYC=<hex>  FRQ=<hex>  CLZ=2B  RBIT=<hex>
# Each line proves a different intrinsic family lowered and ran on the CPU:
# the monotonic-cas spinlock (atomic_cas/load/store), the CPU register reads
# (get_mpidr/cntfrq/cntpct), and the LLVM-intrinsic bit ops (clz/rbit).
verify-intrinsics: $(KERNEL)
	@timeout 5 $(QEMU) $(QEMU_OPTS) -kernel $(KERNEL) > qemu.out 2>&1 || true
	@ok=1; \
	for tag in LOCK CPU CYC FRQ CLZ RBIT; do \
		if grep -q "$$tag=" qemu.out; then \
			line=$$(grep "$$tag=" qemu.out | head -1); \
			echo "[OK] $$line"; \
		else \
			echo "[FAIL] missing $$tag= line"; ok=0; \
		fi; \
	done; \
	if [ "$$ok" -ne 1 ]; then echo "--- full output ---"; cat qemu.out; exit 1; fi

# Boot headlessly and verify the Tier 3 + 4 intrinsic demo lines printed:
#   DAIF=<hex>  SP=<hex>
# Each line proves a different intrinsic family lowered and ran on the CPU
# without trapping: the DAIF interrupt-mask round-trip (get_daif/set_daif/
# disable_irq/enable_irq), the cache maintenance ops (dc_civac/ic_ivau, which
# run as no-ops on QEMU but must not fault), and the stack-pointer read (get_sp).
# Values are CPU-state-dependent, so we only assert the tag prefix appears.
verify-tier3: $(KERNEL)
	@timeout 5 $(QEMU) $(QEMU_OPTS) -kernel $(KERNEL) > qemu.out 2>&1 || true
	@ok=1; \
	for tag in DAIF SP; do \
		if grep -q "$$tag=" qemu.out; then \
			line=$$(grep "$$tag=" qemu.out | head -1); \
			echo "[OK] $$line"; \
		else \
			echo "[FAIL] missing $$tag= line"; ok=0; \
		fi; \
	done; \
	if [ "$$ok" -ne 1 ]; then echo "--- full output ---"; cat qemu.out; exit 1; fi

# Boot headlessly and verify the Tier 5 intrinsic demo lines printed:
#   TLBI=OK  TLBIVA=OK  SPSR=<hex>  ELR=<hex>  ERET=OK
# Each line proves a different Tier 5 primitive lowered and ran on the CPU
# without trapping: the TLB invalidations (tlbi_vmalle1/tlbi_vaae1), the
# exception-return register writes (set_spsr/set_elr, read back via mrs), and
# the safe eret-to-self (eret; if it faulted, ERET=OK would never print).
verify-tier5: $(KERNEL)
	@timeout 5 $(QEMU) $(QEMU_OPTS) -kernel $(KERNEL) > qemu.out 2>&1 || true
	@ok=1; \
	for tag in TLBI TLBIVA SPSR ELR ERET; do \
		if grep -q "$$tag=" qemu.out; then \
			line=$$(grep "$$tag=" qemu.out | head -1); \
			echo "[OK] $$line"; \
		else \
			echo "[FAIL] missing $$tag= line"; ok=0; \
		fi; \
	done; \
	if [ "$$ok" -ne 1 ]; then echo "--- full output ---"; cat qemu.out; exit 1; fi

# Multi-core boot sanity check (2 CPUs). boot.S parks CPU1 in a wfe loop, so
# this confirms the kernel still boots cleanly and prints its banner when more
# than one core is present — it does NOT run Angara on CPU1 (that needs a
# secondary-release path in boot.S, a separate effort). Use verify-intrinsics
# for the functional intrinsic demo.
verify-smp: $(KERNEL)
	@timeout 5 $(QEMU) $(QEMU_OPTS) -smp 2 -kernel $(KERNEL) > qemu.out 2>&1 || true
	@if grep -q "$(BANNER)" qemu.out; then \
		echo "[OK] booted on 2 CPUs, banner printed:"; \
		grep -A1 "$(BANNER)" qemu.out | head -2; \
	else \
		echo "[FAIL] banner not found in 2-CPU boot:"; \
		cat qemu.out; \
		exit 1; \
	fi

clean:
	rm -f *.o *.elf *.img *.ll qemu.out
