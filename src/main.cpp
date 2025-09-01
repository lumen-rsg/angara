#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <memory>

#include "ErrorHandler.h"
#include "Lexer.h"
#include "Parser.h"
#include "TypeChecker.h"

// A helper function to run the static analysis pipeline
void check(const std::string& source) {
    angara::ErrorHandler errorHandler(source);

    // 1. Lexer
    angara::Lexer lexer(source);
    auto tokens = lexer.scanTokens();

    // 2. Parser
    angara::Parser parser(tokens, errorHandler);
    auto statements = parser.parseStmts();
    if (errorHandler.hadError()) {
        std::cerr << "Analysis failed due to parsing errors." << std::endl;
        return;
    }

    // 3. Type Checker
    angara::TypeChecker typeChecker(errorHandler);
    if (typeChecker.check(statements)) {
        std::cout << "Analysis successful: All type checks passed!" << std::endl;
    } else {
        std::cerr << "Analysis failed due to type errors." << std::endl;
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: angara_checker [script]" << std::endl;
        return 64;
    }

    std::string path = argv[1];
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file '" << path << "'" << std::endl;
        return 74;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();

    check(source);

    return 0;
}