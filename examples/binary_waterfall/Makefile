# Binary Waterfall — Angara ImGui demo
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

# Local module cache — angc uses build/modules if it exists, overriding the
# system /opt/angara/modules.  This lets us test a locally-built gui.so
# without sudo.
LOCAL_MODS := .angara/modules
SYS_MODS   := /opt/angara/modules

.PHONY: all run clean modules-sync

all: modules-sync $(BUILD)/binary_waterfall

# Populate build/modules from the system install, then overlay any local overrides.
modules-sync:
	@mkdir -p $(BUILD)/modules
	@if [ -d $(SYS_MODS) ]; then \
		cp -u $(SYS_MODS)/*.so $(BUILD)/modules/ 2>/dev/null || true; \
	fi
	@if [ -d $(LOCAL_MODS) ]; then \
		cp -f $(LOCAL_MODS)/*.so $(BUILD)/modules/ 2>/dev/null || true; \
	fi

$(BUILD)/binary_waterfall: binary_waterfall.an modules-sync
	@mkdir -p $(BUILD)
	$(ANGC) binary_waterfall.an -o $@ --force

run: $(BUILD)/binary_waterfall
	./$(BUILD)/binary_waterfall

clean:
	rm -rf $(BUILD)
