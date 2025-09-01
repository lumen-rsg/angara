cmake_minimum_required(VERSION 4.0)
project(angara_compiled)

set(CMAKE_CXX_STANDARD 23)

find_package(LLVM REQUIRED CONFIG)

add_executable(angara_compiled
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
        src/LLVMCodeGenerator.cpp
)

target_include_directories(angara_compiled PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Link your executable against the LLVM libraries
# We need the core libraries, the code generator, and the JIT compiler.
target_link_libraries(angara_compiled PRIVATE ${LLVM_LIBRARIES} LLVMCore LLVMSupport LLVMIRReader)

# This is a required setting for LLVM's C++ API
add_definitions(${LLVM_DEFINITIONS})