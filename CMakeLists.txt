cmake_minimum_required(VERSION 3.16)
project(angara CXX C)
set(CMAKE_CXX_STANDARD 23)

find_program(CLANG_EXECUTABLE clang)
find_program(CLANG_PLUS_PLUS_EXECUTABLE clang++)

if(CLANG_EXECUTABLE AND CLANG_PLUS_PLUS_EXECUTABLE)
    message(STATUS "Clang found, setting it as the default C/C++ compiler.")
    set(CMAKE_C_COMPILER "clang")
    set(CMAKE_CXX_COMPILER "clang++")
endif()

# --- Dependency Configuration ---
include(FetchContent)

# For macOS, dynamically find the Homebrew root to help CMake and pkg-config.
if(APPLE)
    message(STATUS "macOS detected. Configuring paths for Homebrew.")
    execute_process(COMMAND brew --prefix OUTPUT_VARIABLE HOMEBREW_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(HOMEBREW_PREFIX)
        message(STATUS "Found Homebrew prefix: ${HOMEBREW_PREFIX}")
        list(APPEND CMAKE_PREFIX_PATH ${HOMEBREW_PREFIX})
        set(ENV{PKG_CONFIG_PATH} "${HOMEBREW_PREFIX}/lib/pkgconfig")
    else()
        message(WARNING "Could not determine Homebrew prefix. Build may fail.")
    endif()
endif()

# Find system libraries using pkg-config. This is now macOS-aware.
find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED libcurl)
pkg_check_modules(LWS REQUIRED libwebsockets)
pkg_check_modules(SSL REQUIRED openssl)

if(NOT(APPLE))
    pkg_check_modules(AMQP REQUIRED librabbitmq)
endif ()

if(APPLE)
    # Vendor rabbitmq-c to ensure a consistent, portable build for the amqp module.
    FetchContent_Declare(
            rabbitmq-c
            GIT_REPOSITORY https://github.com/alanxz/rabbitmq-c.git
            GIT_TAG        v0.14.0
    )
    FetchContent_MakeAvailable(rabbitmq-c)
endif ()


# --- Core Library Targets ---

# Build the Angara runtime as a shared library.
add_library(angara_runtime SHARED runtime/angara_runtime.c)
target_include_directories(angara_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/angc/includes)
set_target_properties(angara_runtime PROPERTIES
        PUBLIC_HEADER runtime/angara_runtime.h
        INSTALL_NAME_DIR "@rpath"
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)
set(CMAKE_CXX_FLAGS "-Wno-trigraphs")


# --- Native Module Targets ---
file(GLOB_RECURSE NATIVE_MODULE_SOURCES "${CMAKE_SOURCE_DIR}/modules/*.c")
foreach(MODULE_SOURCE ${NATIVE_MODULE_SOURCES})
    get_filename_component(MODULE_NAME ${MODULE_SOURCE} NAME_WE)
    add_library(${MODULE_NAME} SHARED ${MODULE_SOURCE})

    target_link_libraries(${MODULE_NAME} PRIVATE angara_runtime)

    # Link specific external dependencies for each module.
    if (MODULE_NAME STREQUAL "http")
        target_include_directories(${MODULE_NAME} PRIVATE ${CURL_INCLUDE_DIRS})
        target_link_libraries(${MODULE_NAME} PRIVATE ${CURL_LIBRARIES})

    elseif (MODULE_NAME STREQUAL "websocket")
        target_include_directories(${MODULE_NAME} PRIVATE ${LWS_INCLUDE_DIRS} ${SSL_INCLUDE_DIRS})
        if(APPLE)
            set(SSL_FULL_PATH_LIBS "")
            foreach(SSL_LIB_NAME ${SSL_LIBRARIES})
                list(APPEND SSL_FULL_PATH_LIBS "${SSL_LIBRARY_DIRS}/lib${SSL_LIB_NAME}.dylib")
            endforeach()
            target_link_libraries(${MODULE_NAME} PRIVATE "${LWS_LIBRARY_DIRS}/lib${LWS_LIBRARIES}.dylib" ${SSL_FULL_PATH_LIBS})
        else()
            target_link_libraries(${MODULE_NAME} PRIVATE ${LWS_LIBRARIES} ${SSL_LIBRARIES})
        endif()

    elseif (MODULE_NAME STREQUAL "time")
        if(NOT APPLE)
            target_link_libraries(${MODULE_NAME} PRIVATE rt)
        endif()

    elseif (MODULE_NAME STREQUAL "amqp")
        #if(APPLE)
        #    target_include_directories(${MODULE_NAME} PRIVATE
        #            ${rabbitmq-c_SOURCE_DIR}/librabbitmq
        #            ${rabbitmq-c_BINARY_DIR}
        #    )
        #    target_link_libraries(${MODULE_NAME} PRIVATE rabbitmq-static)
        #else()
        #    target_include_directories(${MODULE_NAME} PRIVATE ${AMQP_INCLUDE_DIRS})
        #    target_link_libraries(${MODULE_NAME} PRIVATE rabbitmq)
        #endif ()
        message("AMQP build on GitHub actions is disabled. To build AMQP Angara module, uncomment the above lines. -cv2")
    endif()

    target_include_directories(${MODULE_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/angc/includes)
    set_target_properties(${MODULE_NAME} PROPERTIES
            INSTALL_NAME_DIR "@rpath"
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    )
    message(STATUS "Configured native Angara module: ${MODULE_NAME}")
endforeach()


# --- Main Executable Targets ---

# Build the angc compiler.
file(GLOB_RECURSE ANGC_SOURCES "${CMAKE_SOURCE_DIR}/angc/*.cpp")
add_executable(angc ${ANGC_SOURCES})
target_include_directories(angc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/angc/includes)
target_link_libraries(angc PRIVATE angara_runtime)

# Build the angara-ls language server.
file(GLOB_RECURSE ALS_SOURCES "${CMAKE_SOURCE_DIR}/angara-ls/*.cpp")
file(GLOB_RECURSE ANGC_FRONTEND
        "${CMAKE_SOURCE_DIR}/angc/analyzer/*.cpp"
        "${CMAKE_SOURCE_DIR}/angc/frontend/*.cpp"
        "${CMAKE_SOURCE_DIR}/angc/shared/*.cpp"
        "${CMAKE_SOURCE_DIR}/angc/backend/*.cpp"
)
add_executable(angara_ls ${ALS_SOURCES} ${ANGC_FRONTEND})
target_include_directories(angara_ls PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/angara-ls/includes)
target_include_directories(angara_ls PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/angc/includes)
target_include_directories(angara_ls PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/angara-ls/vendor)


# --- Testing Target ---
add_custom_target(run_test
        DEPENDS angc fs
        COMMAND ${CMAKE_BINARY_DIR}/angc ${CMAKE_SOURCE_DIR}/native_test.an
        COMMAND gcc -o native_test_executable native_test.c -L${CMAKE_BINARY_DIR}/lib -langara_runtime -lfs -I. -pthread -lm
        COMMAND ./native_test_executable
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running Angara end-to-end test"
)