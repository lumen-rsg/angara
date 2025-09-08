
cmake_minimum_required(VERSION 3.16)
project(angara_compiler CXX C)
set(CMAKE_CXX_STANDARD 23)

# --- Define the Angara C Runtime as a Library ---
# This makes it easier to link against.
add_library(angara_runtime src/runtime/angara_runtime.c)
target_include_directories(angara_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime)


# --- Define the main Angara Compiler Executable ---
add_executable(angara_compiler
        src/ErrorHandler.cpp
        src/Lexer.cpp
        src/main.cpp
        src/Parser.cpp
        src/Token.cpp
        src/AngaraClass.cpp
        src/AngaraInstance.cpp
        src/AngaraTrait.cpp
        src/AngaraClosure.cpp
        src/TypeChecker.cpp
        src/SymbolTable.cpp
        src/CTranspiler.cpp
        src/ASTPrinter.cpp
        src/CompilerDriver.cpp
)
# The compiler needs to link against the runtime to be self-contained (if ever needed)
# and needs the pthreads library for its own logic if it uses threads.
target_link_libraries(angara_compiler PRIVATE angara_runtime pthread)


target_include_directories(angara_compiler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Set the build rpath to include the directory where the executable is.
# This tells the OS to look for .dylib files in the same folder as the binary.
set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)
set(CMAKE_INSTALL_RPATH "@executable_path")

# ====================================================================
# --- Automatic Native Module Compilation ---
# ====================================================================

# 1. Define the path to your public C ABI header.
set(ANGARA_ABI_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/include/AngaraABI.h)

# 2. Find all C source files in the 'modules' directory.
#    The GLOB_RECURSE will find files in subdirectories too.
file(GLOB_RECURSE NATIVE_MODULE_SOURCES "${CMAKE_SOURCE_DIR}/modules/*.c")

# 3. Loop through each found source file and create a shared library target.
foreach(MODULE_SOURCE ${NATIVE_MODULE_SOURCES})
    # Get the base name of the module file (e.g., "modules/fs.c" -> "fs")
    get_filename_component(MODULE_NAME ${MODULE_SOURCE} NAME_WE)

    # Add a target to build this source file into a shared library (.so)
    add_library(${MODULE_NAME} SHARED ${MODULE_SOURCE})

    # --- THIS IS A CRUCIAL ADDITION for macOS ---
    # Explicitly set the library's "install name". This is metadata inside
    # the .dylib that tells the linker where to find it at runtime.
    # "@rpath/" tells it to use the executable's rpath settings.
    set_target_properties(${MODULE_NAME} PROPERTIES
            INSTALL_NAME_DIR "@rpath"
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/modules
    )

    # Tell this module where to find the public Angara ABI header.
    target_include_directories(${MODULE_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

    # All native modules must link against the Angara runtime library.
    target_link_libraries(${MODULE_NAME} PRIVATE angara_runtime)

    # Optional: Set a specific output directory for the compiled modules.
    # This keeps your build directory clean.
    set_target_properties(${MODULE_NAME} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/modules)

    # Print a status message so you can see it's working
    message(STATUS "Configured native Angara module: ${MODULE_NAME}")
endforeach()

# ====================================================================


