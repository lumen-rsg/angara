# Mandelbrot Explorer — Angara ImGui demo
#
# Prerequisites:
#   - `make modules` from the repo root (builds modules/gui.so)
#   - OpenGL-capable display (GL dev packages installed)
#
# Usage:
#   make           — Build the binary
#   make run       — Build and run
#   make clean     — Remove build artifacts

ANGC  ?= ../../build/angc
BUILD  = build

LOCAL_MODS := .angara/modules
SYS_MODS   := /opt/angara/modules

.PHONY: all run clean modules-sync

all: modules-sync $(BUILD)/mandelbrot

modules-sync:
	@mkdir -p $(BUILD)/modules
	@if [ -d $(SYS_MODS) ]; then \
		cp -u $(SYS_MODS)/*.so $(BUILD)/modules/ 2>/dev/null || true; \
	fi
	@if [ -d $(LOCAL_MODS) ]; then \
		cp -f $(LOCAL_MODS)/*.so $(BUILD)/modules/ 2>/dev/null || true; \
	fi

$(BUILD)/mandelbrot: mandelbrot.an modules-sync
	@mkdir -p $(BUILD)
	$(ANGC) mandelbrot.an -o $@ --force

run: $(BUILD)/mandelbrot
	./$(BUILD)/mandelbrot

clean:
	rm -rf $(BUILD)
