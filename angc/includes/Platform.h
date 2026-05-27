#pragma once

// Shared platform detection constants used by the compiler driver and build system.

#if defined(__APPLE__)
    #define ANGARA_NATIVE_EXT ".dylib"
    #define ANGARA_SO_EXT ".dylib"
#elif defined(__linux__)
    #define ANGARA_NATIVE_EXT ".so"
    #define ANGARA_SO_EXT ".so"
#elif defined(_WIN32)
    #define ANGARA_NATIVE_EXT ".dll"
    #define ANGARA_SO_EXT ".dll"
#else
    #define ANGARA_NATIVE_EXT ".so"
    #define ANGARA_SO_EXT ".so"
#endif
