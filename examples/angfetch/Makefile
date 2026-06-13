# angfetch — fastfetch-style system info tool
#
# Prerequisites:
#   - angc (Angara compiler)
#
# Usage:
#   make          — Build angfetch
#   make run      — Build and run
#   make clean    — Remove build artifacts

ANGC  ?= ../../build/angc
BUILD  = build

.PHONY: all run clean

all: $(BUILD)/angfetch

$(BUILD)/angfetch: angfetch.an | $(BUILD)
	$(ANGC) angfetch.an -o $@

$(BUILD):
	mkdir -p $(BUILD)

run: $(BUILD)/angfetch
	./$(BUILD)/angfetch

clean:
	rm -rf $(BUILD)
