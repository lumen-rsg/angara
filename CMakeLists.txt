cmake_minimum_required(VERSION 3.16)
project(angara CXX C) # Project is now 'angara'
set(CMAKE_CXX_STANDARD 23)

# --- NEW: Tell CMake to use pkg-config ---
find_package(PkgConfig REQUIRED)

# --- Find libraries using pkg-config ---
# This will find libcurl and set variables like CURL_INCLUDE_DIRS and CURL_LIBRARIES
pkg_check_modules(CURL REQUIRED libcurl)

pkg_check_modules(LWS REQUIRED libwslay)    # <-- CHANGE to libwslay
pkg_check_modules(SSL REQUIRED openssl)   # <-- ADD openssl

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

    # --- THE FIX: Conditional Linking ---
    # If this specific module needs an external library, link it here.
    if (MODULE_NAME STREQUAL "http")
        message(STATUS "Module 'http' requires libcurl. Linking...")
        # Use the variables found by pkg-config
        target_include_directories(${MODULE_NAME} PRIVATE ${CURL_INCLUDE_DIRS})
        target_link_libraries(${MODULE_NAME} PRIVATE ${CURL_LIBRARIES})

    elseif (MODULE_NAME STREQUAL "websocket")
        message(STATUS "Module 'websocket' requires libwslay and openssl. Linking...")
        # --- THE FIX ---
        target_link_libraries(${MODULE_NAME} PRIVATE ${LWS_LIBRARIES} ${SSL_LIBRARIES})
    elseif (MODULE_NAME STREQUAL "time") # <-- ADD THIS BLOCK
        message(STATUS "Module 'time' requires the real-time library. Linking...")
        # On many systems, clock_gettime requires linking with librt
        target_link_libraries(${MODULE_NAME} PRIVATE rt)
    endif()
    # --- END FIX ---

    target_include_directories(${MODULE_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_target_properties(${MODULE_NAME} PROPERTIES
            INSTALL_NAME_DIR "@rpath"
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    )
    message(STATUS "Configured native Angara module: ${MODULE_NAME}")
endforeach()


# --- 3. Build the angc executable ---
add_executable(angc
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
target_include_directories(angc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(angc PRIVATE angara_runtime)


# --- 4. A new target to run tests ---
# This will build and run the transpiled code.
add_custom_target(run_test
        # First, build the compiler and all libraries
        DEPENDS angc fs # Add other modules here

        # Then, run the compilation process
        COMMAND ${CMAKE_BINARY_DIR}/angc ${CMAKE_SOURCE_DIR}/native_test.an

        # Then, compile the generated C code, linking against our runtime library
        COMMAND gcc -o native_test_executable native_test.c -L${CMAKE_BINARY_DIR}/lib -langara_runtime -lfs -I. -pthread -lm

        # Finally, run the executable
        COMMAND ./native_test_executable

        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running Angara end-to-end test"
)