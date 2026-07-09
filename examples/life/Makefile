# Game of Life — Angara ImGui demo
ANGC  ?= ../../build/angc
BUILD  = build
LOCAL_MODS := .angara/modules
SYS_MODS   := /opt/angara/modules

.PHONY: all run clean modules-sync

all: modules-sync $(BUILD)/life

modules-sync:
	@mkdir -p $(BUILD)/modules
	@if [ -d $(SYS_MODS) ]; then cp -u $(SYS_MODS)/*.so $(BUILD)/modules/ 2>/dev/null || true; fi
	@if [ -d $(LOCAL_MODS) ]; then cp -f $(LOCAL_MODS)/*.so $(BUILD)/modules/ 2>/dev/null || true; fi

$(BUILD)/life: life.an modules-sync
	@mkdir -p $(BUILD)
	$(ANGC) life.an -o $@ --force

run: $(BUILD)/life
	./$(BUILD)/life

clean:
	rm -rf $(BUILD)
