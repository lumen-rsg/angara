cmake_minimum_required(VERSION 4.0)
project(angara_compiled)

set(CMAKE_CXX_STANDARD 23)

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
)

target_include_directories(angara_compiled PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)