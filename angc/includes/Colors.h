#pragma once

// Centralized ANSI color constants for the Angara compiler toolchain.
// Use these everywhere instead of defining local constants.

const char* const CLR_RESET   = "\033[0m";
const char* const CLR_BOLD    = "\033[1m";
const char* const CLR_DIM     = "\033[2m";

const char* const CLR_RED     = "\033[31m";
const char* const CLR_GREEN   = "\033[32m";
const char* const CLR_YELLOW  = "\033[33m";
const char* const CLR_BLUE    = "\033[34m";
const char* const CLR_MAGENTA = "\033[35m";
const char* const CLR_CYAN    = "\033[36m";
const char* const CLR_WHITE   = "\033[97m";
const char* const CLR_GRAY    = "\033[90m";
