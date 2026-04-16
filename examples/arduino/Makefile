# Angara Arduino Build System
# Compiles Angara code for Arduino boards via AVR toolchain
#
# Usage:
#   make blink          # Compile blink.an for Arduino Uno
#   make upload         # Compile and upload to board
#   make clean          # Remove build artifacts
#   make BOARD=nano33   # Target Arduino Nano 33 (ARM Cortex-M0+)

# ─── Board Configuration ───────────────────────────────────────────
BOARD    ?= uno
BAUD     ?= 115200
PORT     ?= /dev/tty.usbmodem1401

# ─── Toolchain Detection ───────────────────────────────────────────
ANGC     ?= angc

# Board-specific settings
ifeq ($(BOARD),uno)
    # Arduino Uno / ATmega328P
    TARGET_TRIPLE = avr-atmel-none
    MCU           = atmega328p
    F_CPU         = 16000000UL
    AVRDUDE_MCU   = m328p
    AVRDUDE_PROG  = arduino
    OBJCOPY_FLAGS = -O ihex -R .eeprom
    CC            = avr-gcc
    CFLAGS        = -mmcu=$(MCU) -DF_CPU=$(F_CPU) -Os -Wall -ffunction-sections -fdata-sections
    LDFLAGS       = -mmcu=$(MCU) -Os -Wl,--gc-sections
    ARDUINO_CORE  = /usr/local/share/arduino/hardware/arduino/avr/cores/arduino
    HAS_ARDUINO   = $(shell test -d $(ARDUINO_CORE) && echo yes)
else ifeq ($(BOARD),nano)
    # Arduino Nano (Classic, ATmega328P)
    TARGET_TRIPLE = avr-atmel-none
    MCU           = atmega328p
    F_CPU         = 16000000UL
    AVRDUDE_MCU   = m328p
    AVRDUDE_PROG  = arduino
    OBJCOPY_FLAGS = -O ihex -R .eeprom
    CC            = avr-gcc
    CFLAGS        = -mmcu=$(MCU) -DF_CPU=$(F_CPU) -Os -Wall -ffunction-sections -fdata-sections
    LDFLAGS       = -mmcu=$(MCU) -Os -Wl,--gc-sections
    ARDUINO_CORE  = /usr/local/share/arduino/hardware/arduino/avr/cores/arduino
    HAS_ARDUINO   = $(shell test -d $(ARDUINO_CORE) && echo yes)
else ifeq ($(BOARD),mega)
    # Arduino Mega 2560
    TARGET_TRIPLE = avr-atmel-none
    MCU           = atmega2560
    F_CPU         = 16000000UL
    AVRDUDE_MCU   = m2560
    AVRDUDE_PROG  = wiring
    OBJCOPY_FLAGS = -O ihex -R .eeprom
    CC            = avr-gcc
    CFLAGS        = -mmcu=$(MCU) -DF_CPU=$(F_CPU) -Os -Wall -ffunction-sections -fdata-sections
    LDFLAGS       = -mmcu=$(MCU) -Os -Wl,--gc-sections
    ARDUINO_CORE  = /usr/local/share/arduino/hardware/arduino/avr/cores/arduino
    HAS_ARDUINO   = $(shell test -d $(ARDUINO_CORE) && echo yes)
else ifeq ($(BOARD),nano33)
    # Arduino Nano 33 IoT (ARM Cortex-M0+)
    TARGET_TRIPLE = arm-none-eabi
    MCU           = samd21g18a
    F_CPU         = 48000000UL
    CC            = arm-none-eabi-gcc
    CFLAGS        = -mcpu=cortex-m0plus -mthumb -DF_CPU=$(F_CPU) -Os -Wall
    LDFLAGS       = -mcpu=cortex-m0plus -mthumb -Os -nostdlib
    ARDUINO_CORE  =
    HAS_ARDUINO   = no
else ifeq ($(BOARD),esp32)
    # ESP32 (Xtensa)
    TARGET_TRIPLE = xtensa-esp32-elf
    MCU           = esp32
    F_CPU         = 240000000UL
    CC            = xtensa-esp32-elf-gcc
    CFLAGS        = -DESP_PLATFORM -DF_CPU=$(F_CPU) -Os -Wall
    LDFLAGS       = -DESP_PLATFORM -Os -nostdlib
    ARDUINO_CORE  =
    HAS_ARDUINO   = no
endif

# ─── Project Sources ───────────────────────────────────────────────
# Set SRC to your Angara source file, e.g.: make blink SRC=blink.an
SRC  ?= blink.an
NAME := $(basename $(SRC))

BUILD_DIR = build

# ─── Build Targets ─────────────────────────────────────────────────

.PHONY: all compile upload clean flash serial $(NAME)

all: compile

# Step 1: Compile Angara source to object file (LLVM backend)
$(BUILD_DIR)/ang_main.o: $(SRC) | $(BUILD_DIR)
	$(ANGC) --freestanding --target $(TARGET_TRIPLE) $(SRC)
	@mkdir -p $(BUILD_DIR)
	mv ang_main.o $(BUILD_DIR)/
	mv ang_main.ll $(BUILD_DIR)/ 2>/dev/null || true

# Step 2: Compile C shim
$(BUILD_DIR)/arduino_core.o: arduino_core.c arduino_core.h | $(BUILD_DIR)
ifeq ($(HAS_ARDUINO),yes)
	$(CC) $(CFLAGS) -I$(ARDUINO_CORE) -I. -c -o $@ $<
else
	$(CC) $(CFLAGS) -I. -c -o $@ $<
endif

# Step 3: Link everything into ELF
$(BUILD_DIR)/$(NAME).elf: $(BUILD_DIR)/ang_main.o $(BUILD_DIR)/arduino_core.o
	$(CC) $(LDFLAGS) -o $@ $^ -lm

# Step 4: Convert to HEX
$(BUILD_DIR)/$(NAME).hex: $(BUILD_DIR)/$(NAME).elf
ifeq ($(findstring avr,$(TARGET_TRIPLE)),avr)
	avr-objcopy $(OBJCOPY_FLAGS) $< $@
	avr-size $<
else
	cp $< $@
endif

# Full compile chain
compile: $(BUILD_DIR)/$(NAME).hex
	@echo ""
	@echo "✅ Build successful for $(BOARD) ($(TARGET_TRIPLE))"
	@echo "   ELF: $(BUILD_DIR)/$(NAME).elf"
	@echo "   HEX: $(BUILD_DIR)/$(NAME).hex"

# Upload to board via avrdude (AVR only)
upload: compile
ifeq ($(findstring avr,$(TARGET_TRIPLE)),avr)
	avrdude -F -V -c $(AVRDUDE_PROG) -p $(AVRDUDE_MCU) -P $(PORT) -b $(BAUD) -U flash:w:$(BUILD_DIR)/$(NAME).hex:i
	@echo "📤 Uploaded to $(BOARD) on $(PORT)"
else
	@echo "⚠️  Direct upload not supported for $(BOARD). Flash manually."
endif

# Alias
flash: upload

# Serial monitor
serial:
	screen $(PORT) $(BAUD)

# Clean
clean:
	rm -rf $(BUILD_DIR) ang_main.o ang_main.ll

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)