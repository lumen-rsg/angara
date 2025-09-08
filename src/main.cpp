#include <iostream>
#include <string>
#include "CompilerDriver.h"

const char* const RESET = "\033[0m";
const char* const BOLD = "\033[1m";
const char* const RED = "\033[31m";
const char* const GREEN = "\033[32m";

int main(int argc, char* argv[]) {
    // 1. Basic command-line argument validation.
    if (argc != 2) {
        std::cerr << RED << BOLD << "Error: " << RESET
                  << "Incorrect usage." << std::endl;
        std::cerr << "Usage: angara_compiler <source_file.an>" << std::endl;
        return 1;
    }

    // 2. Create an instance of the compiler driver.
    angara::CompilerDriver driver;

    // 3. Kick off the compilation process for the root file.
    bool success = driver.compile(argv[1]);

    // 4. Return the appropriate exit code.
    if (success) {
        std::cout << BOLD << GREEN << "\nBuild successful." << RESET << std::endl;
        return 0;
    } else {
        std::cout << BOLD << RED << "\nBuild failed." << RESET << std::endl;
        return 1;
    }
}