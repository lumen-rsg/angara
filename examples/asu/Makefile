# asu — Angara Super User (simple sudo clone)
#
# Prerequisites:
#   - angc (Angara compiler)
#
# Usage:
#   make          — Build asu
#   make install  — Set the setuid bit (requires sudo)
#   make run      — Build and run
#   make clean    — Remove build artifacts

ANGC  ?= ../../build/angc
BUILD  = build

.PHONY: all install run clean

all: $(BUILD)/asu

$(BUILD)/asu: asu.an | $(BUILD)
	$(ANGC) asu.an -o $@

$(BUILD):
	mkdir -p $(BUILD)

install: $(BUILD)/asu
	sudo chown root $(BUILD)/asu
	sudo chmod u+s $(BUILD)/asu
	@echo "Installed setuid root: $(BUILD)/asu"

run: $(BUILD)/asu
	./$(BUILD)/asu

clean:
	rm -rf $(BUILD)
