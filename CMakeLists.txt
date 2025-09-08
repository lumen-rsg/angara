cmake_minimum_required(VERSION 3.16)
project(angara CXX C) # Project is now 'angara'
set(CMAKE_CXX_STANDARD 23)

# --- 1. Build libangara_runtime.so ---
# The runtime is now a SHARED library that everything else will link against.
add_library(angara_runtime SHARED src/runtime/angara_runtime.c)
target_include_directories(angara_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(angara_runtime PROPERTIES
        PUBLIC_HEADER src/runtime/angara_runtime.h # Expose the header
        INSTALL_NAME_DIR "@rpath"
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)

# --- 2. Build Native Modules (like libfs.so) ---
file(GLOB_RECURSE NATIVE_MODULE_SOURCES "${CMAKE_SOURCE_DIR}/modules/*.c")
foreach(MODULE_SOURCE ${NATIVE_MODULE_SOURCES})
    get_filename_component(MODULE_NAME ${MODULE_SOURCE} NAME_WE)
    add_library(${MODULE_NAME} SHARED ${MODULE_SOURCE})

    # Modules link against the CORE RUNTIME LIBRARY
    target_link_libraries(${MODULE_NAME} PRIVATE angara_runtime)

    target_include_directories(${MODULE_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_target_properties(${MODULE_NAME} PROPERTIES
            INSTALL_NAME_DIR "@rpath"
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    )
    message(STATUS "Configured native Angara module: ${MODULE_NAME}")
endforeach()


# --- 3. Build the angara_compiler executable ---
add_executable(angara_compiler
        src/ErrorHandler.cpp
        src/Lexer.cpp
        src/main.cpp
        src/Parser.cpp
        src/Token.cpp
        src/TypeChecker.cpp
        src/SymbolTable.cpp
        src/CTranspiler.cpp
        src/ASTPrinter.cpp
        src/CompilerDriver.cpp
)
# The compiler itself does not need to link against the runtime.
target_include_directories(angara_compiler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(angara_compiler PRIVATE angara_runtime)


# --- 4. A new target to run tests ---
# This will build and run the transpiled code.
add_custom_target(run_test
        # First, build the compiler and all libraries
        DEPENDS angara_compiler fs # Add other modules here

        # Then, run the compilation process
        COMMAND ${CMAKE_BINARY_DIR}/angara_compiler ${CMAKE_SOURCE_DIR}/native_test.an

        # Then, compile the generated C code, linking against our runtime library
        COMMAND gcc -o native_test_executable native_test.c -L${CMAKE_BINARY_DIR}/lib -langara_runtime -lfs -I. -pthread -lm

        # Finally, run the executable
        COMMAND ./native_test_executable

        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running Angara end-to-end test"
)