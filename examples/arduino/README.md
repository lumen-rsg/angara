# Angara on Arduino

Run Angara code on Arduino boards! This example demonstrates how to compile Angara programs for embedded targets using the LLVM backend's cross-compilation support and the `foreign func` C ABI bridge.

## How It Works

```
┌─────────────────┐     ┌──────────────────┐     ┌─────────────────┐
│  blink.an        │     │  arduino_core.c   │     │  Arduino SDK    │
│  (Angara source) │     │  (C shim)         │     │  (C/C++ libs)   │
└────────┬────────┘     └────────┬──────────┘     └────────┬────────┘
         │                       │                          │
    angc --freestanding     avr-gcc -c                  avr-gcc -c
    --target avr                  │                          │
         │                       │                          │
         ▼                       ▼                          ▼
   ┌──────────┐           ┌──────────────┐          ┌──────────────┐
   │ ang_main │           │ arduino_core │          │ arduino libs │
   │   .o     │           │     .o       │          │     .o       │
   └─────┬────┘           └──────┬───────┘          └──────┬───────┘
         │                       │                          │
         └───────────────────────┼──────────────────────────┘
                                 │
                           avr-gcc (static link)
                                 │
                                 ▼
                        ┌──────────────┐
                        │  blink.elf   │
                        └──────┬───────┘
                               │
                        avr-objcopy
                               │
                               ▼
                        ┌──────────────┐
                        │  blink.hex   │ ──→ avrdude ──→ Arduino board
                        └──────────────┘
```

### The Foreign Function Bridge

Angara's `foreign func` declarations generate thin LLVM IR wrappers that:

1. **Declare** the C function with native types: `declare void @pinMode(i64, i64)`
2. **Wrap** it with an AngaraObject ABI: `define @__ang_main_pinMode(AngaraObject, AngaraObject) -> AngaraObject`
3. The wrapper **extracts** native C values from AngaraObject params, **calls** the C function, and **wraps** the result

This means `digitalWrite(pin, val)` in Angara directly calls the Arduino SDK's `digitalWrite()` — no runtime overhead beyond the thin type conversion.

### The C Shim

The `arduino_core.c` file provides the Arduino entry points (`setup()` and `loop()`) that forward to Angara-generated functions (`__ang_main_setup()` and `__ang_main_loop()`).

## Prerequisites

1. **Angara compiler** (`angc`) — built and in your `PATH`
2. **AVR toolchain**:
   ```bash
   # macOS
   brew tap osx-cross/avr
   brew install avr-gcc avrdude

   # Linux (Ubuntu/Debian)
   sudo apt install gcc-avr avr-libc avrdude
   ```
3. **Arduino SDK** (optional, for full SDK access) — install from [arduino.cc](https://www.arduino.cc/en/software)

## Quick Start

```bash
cd examples/arduino

# Compile the blink example
make compile SRC=blink.an

# Upload to Arduino Uno (adjust PORT in Makefile)
make upload SRC=blink.an

# Clean build artifacts
make clean
```

## Writing Arduino Programs in Angara

### Basic Pattern

Every Arduino program needs a `setup()` and `loop()` function:

```angara
// Declare C functions from the Arduino SDK
foreign func pinMode(pin as i64, mode as i64) -> nil;
foreign func digitalWrite(pin as i64, val as i64) -> nil;
foreign func delay(ms as i64) -> nil;

let OUTPUT as i64 = 1;
let HIGH as i64 = 1;
let LOW as i64 = 0;
let LED_BUILTIN as i64 = 13;

func setup() -> nil {
    pinMode(LED_BUILTIN, OUTPUT);
}

func loop() -> nil {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(1000);
    digitalWrite(LED_BUILTIN, LOW);
    delay(1000);
}
```

### Using the Arduino Module

For convenience, use the `arduino.an` module which pre-declares all common functions:

```angara
attach "arduino";

func setup() -> nil {
    pinMode(LED_BUILTIN, OUTPUT);
    serial_begin(9600);
}

let count as i64 = 0;

func loop() -> nil {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(500);
    digitalWrite(LED_BUILTIN, LOW);
    delay(500);

    count = count + 1;
    serial_println_i64(count);
}
```

### Supported Foreign Types

The `foreign func` bridge supports these type mappings:

| Angara Type | C Type      | Notes                          |
|-------------|-------------|--------------------------------|
| `i8`        | `int8_t`    |                                |
| `i16`       | `int16_t`   |                                |
| `i32`       | `int32_t`   |                                |
| `i64`       | `int64_t`   | Default if type omitted        |
| `f32`       | `float`     |                                |
| `f64`       | `double`    |                                |
| `bool`      | `bool`/`i1` |                                |
| `string`    | `const char*`| Passed as pointer             |
| `nil`/void  | `void`      | Return type only               |

## Supported Boards

| Board           | `BOARD=`   | Target Triple           | MCU             | Status |
|-----------------|------------|------------------------|-----------------|--------|
| Arduino Uno     | `uno`      | `avr-atmel-none`       | ATmega328P      | ✅     |
| Arduino Nano    | `nano`     | `avr-atmel-none`       | ATmega328P      | ✅     |
| Arduino Mega    | `mega`     | `avr-atmel-none`       | ATmega2560      | ✅     |
| Nano 33 IoT     | `nano33`   | `arm-none-eabi`        | SAMD21          | 🧪     |
| ESP32           | `esp32`    | `xtensa-esp32-elf`     | ESP32           | 🧪     |

### Board-Specific Build

```bash
# Arduino Uno (default)
make compile SRC=blink.an BOARD=uno

# Arduino Mega
make compile SRC=blink.an BOARD=mega

# ESP32
make compile SRC=blink.an BOARD=esp32
```

## Memory Considerations

- **ATmega328P** (Uno/Nano): 2KB SRAM, 32KB Flash
- `AngaraObject` is 12 bytes on AVR (no alignment padding)
- The freestanding runtime stubs are minimal (no heap allocation)
- Prefer `i64` for all numeric values — smaller types just truncate
- Avoid creating many local variables in `loop()` — they're stack-allocated

## Adding New Foreign Functions

To call a new Arduino SDK function:

1. Add a `foreign func` declaration in your `.an` file:
   ```angara
   foreign func map(value as i64, fromLow as i64, fromHigh as i64, toLow as i64, toHigh as i64) -> i64;
   ```

2. If using the C shim (non-AVR targets), add the implementation in `arduino_core.c`:
   ```c
   int64_t map(int64_t value, int64_t fromLow, int64_t fromHigh, int64_t toLow, int64_t toHigh) {
       return (value - fromLow) * (toHigh - toLow) / (fromHigh - fromLow) + toLow;
   }
   ```

3. On AVR, the Arduino SDK already provides `map()` — it's linked automatically.

## Troubleshooting

- **"No target" error**: LLVM's AVR backend may not be built. Ensure your `angc` was compiled with AVR support.
- **Upload fails**: Check the serial port (`PORT` in Makefile). On Linux it's usually `/dev/ttyUSB0`, on macOS `/dev/tty.usbmodem*`.
- **Undefined reference to `__ang_main_setup`**: The C shim expects these functions. Make sure your Angara code defines `setup()` and `loop()` functions.
- **Size issues**: Run `avr-size build/<name>.elf` to check flash/RAM usage.