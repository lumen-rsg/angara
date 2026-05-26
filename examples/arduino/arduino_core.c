// Angara Arduino C Shim — Source
// This file is the bridge between the Arduino SDK and Angara-compiled code.
//
// Build flow:
//   1. angc --freestanding --target avr blink.an → ang_main.o
//      (contains __ang_main_setup, __ang_main_loop, __ang_main_pinMode, etc.)
//   2. avr-gcc compiles this file → arduino_core.o
//   3. avr-gcc links both with Arduino SDK → blink.elf
//   4. avr-objcopy → blink.hex → avrdude uploads to board
//
// The Arduino SDK provides main(), which calls setup() once, then loop() forever.
// We implement setup() and loop() here, forwarding to Angara-generated wrappers.
// The Angara wrappers (__ang_main_setup, __ang_main_loop) handle type conversion
// between AngaraObject and C native types when calling foreign functions.

#include "arduino_core.h"

extern AngaraObject __ang_main_setup(void);
extern AngaraObject __ang_main_loop(void);

void setup(void) {
    // Call Angara's setup function
    __ang_main_setup();
}

void loop(void) {
    // Call Angara's loop function repeatedly (Arduino calls this forever)
    __ang_main_loop();
}