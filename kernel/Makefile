# ============================================================================
# kernel/Makefile — builds an Angara-sourced Linux kernel module.
#
# Prerequisites:
#   - A kernel build tree at KDIR. The CC must match how the running kernel was
#     built: a gcc-built kernel needs CC=gcc (gcc-built CFLAGS aren't clang-
#     compatible); a clang-built kernel uses CC=clang. Angara objects are LLVM-
#     emitted but link cleanly against either because they're plain relocatable
#     ELF with standard relocations.
#   - `angc` (the Angara compiler) on PATH, or set ANGC=/path/to/angc.
#
# Usage:
#   make                 # compile driver.an → angara_module.o, then kbuild → angara_drv.ko
#   make load            # insmod angara_drv.ko (creates /dev/angara via device_create)
#   make unload          # rmmod angara_drv
#   make test            # echo/cat round-trip through /dev/angara
#   make clean           # remove kernel build artifacts
#
# NOTE on `make`: if a ZCode AppImage mount shadows system `make` in PATH, the
# kbuild recursion picks up the wrong binary. The wrapper below forces the real
# GNU make (resolved once at the top level) into the recursion via MAKE=.
# ============================================================================

ANGC ?= angc
KDIR ?= /lib/modules/$(shell uname -r)/build
# CC must match how the running kernel was built: a gcc-built kernel needs
# CC=gcc (its CFLAGS use gcc-specific flags); a clang-built kernel uses CC=clang.
# gcc is the common case (most distro kernels, incl. asahi) — verify with
# `grep -m1 gcc /proc/version`. Angara objects are plain relocatable ELF and
# link cleanly against either.
CC   ?= gcc

# Auto-resolve the Angara compiler if `angc` isn't on PATH (typical from a
# fresh clone). This file is at <repo>/kernel/Makefile, so build/angc is two
# levels up. Computed relative to this file; exported for kbuild recursion.
KERNEL_MK_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
ifeq ($(wildcard $(KERNEL_MK_DIR)/../build/angc),)
  $(info [kernel] angc not found at $(KERNEL_MK_DIR)/../build/angc — set ANGC=/path/to/angc)
else
  ANGC := $(KERNEL_MK_DIR)/../build/angc
endif
export ANGC

# Resolve the real GNU make once (avoid an AppImage `make` shadow in PATH).
REAL_MAKE := $(firstword $(wildcard /usr/bin/make /usr/bin/gmake $(shell command -v make 2>/dev/null)))

# The Angara source and the object it compiles to. driver.an → angara_module.o
ANGARA_SRC := examples/driver.an
ANGARA_OBJ := angara_module.o

obj-m := angara_drv.o
# Only the C sources go in angara_drv-y. The prebuilt Angara object is handed to
# kbuild separately (always-y + a satisfied target), so kbuild never tries to
# compile it from a nonexistent .c.
angara_drv-y := driver_glue.o kernel_runtime.o
angara_drv-y += $(ANGARA_OBJ)

# Tell kbuild angara_module.o is already built (no .c to compile from).
$(src)/$(ANGARA_OBJ): $(ANGARA_SRC) FORCE
	$(ANGC) --kernel $(ANGARA_SRC) -o $(src)/$(ANGARA_OBJ)

.PHONY: all modules load unload test clean

all: modules

modules:
	$(REAL_MAKE) -C $(KDIR) M=$(CURDIR) CC=$(CC) MAKE=$(REAL_MAKE) modules

load:
	$(MAKE) all
	sudo insmod angara_drv.ko 2>/dev/null || true

unload:
	sudo rmmod angara_drv 2>/dev/null || true
	sudo rm -f /dev/angara

test: load
	@echo "→ echo 7 > /dev/angara";   echo 7 > /dev/angara || true
	@echo "→ head -c8 /dev/angara";   head -c8 /dev/angara | od -An -tx8 || true
	$(MAKE) unload

clean:
	-$(REAL_MAKE) -C $(KDIR) M=$(CURDIR) MAKE=$(REAL_MAKE) clean 2>/dev/null
	rm -f $(ANGARA_OBJ) angara_drv.ko angara_drv.o angara_drv.mod* driver_glue.o kernel_runtime.o
	rm -f Module.symvers modules.order .*.cmd
