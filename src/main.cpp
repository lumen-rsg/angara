#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <memory>

#include "ErrorHandler.h"
#include "Lexer.h"
#include "Parser.h"
#include "TypeChecker.h" // The star of the show!
#include "LLVMCodeGenerator.h"

// A helper function to run the static analysis pipeline
void check(const std::string& source) {
    angara::ErrorHandler errorHandler(source);

    // 1. Lexer
    angara::Lexer lexer(source);
    std::vector<angara::Token> tokens = lexer.scanTokens();

    // 2. Parser
    angara::Parser parser(tokens, errorHandler);
    std::vector<std::shared_ptr<angara::Stmt>> statements = parser.parseStmts();

    // Stop if there were parsing errors.
    if (errorHandler.hadError()) {
        std::cerr << "Analysis failed due to parsing errors." << std::endl;
        return;
    }

    // 3. Type Checker
    angara::TypeChecker typeChecker(errorHandler);
    bool passed = typeChecker.check(statements);

    if (passed) {
        std::cout << "Analysis successful: All type checks passed!" << std::endl;
    } else {
        std::cerr << "Analysis failed due to type errors." << std::endl;
    }
}


void compile(const std::string& source) {
    angara::ErrorHandler errorHandler(source);

    // 1. Lexer & Parser (Frontend)
    angara::Lexer lexer(source);
    auto tokens = lexer.scanTokens();
    angara::Parser parser(tokens, errorHandler);
    auto statements = parser.parseStmts();
    if (errorHandler.hadError()) return;

    // 2. Type Checker (Semantic Analysis)
    angara::TypeChecker typeChecker(errorHandler);
    if (!typeChecker.check(statements)) {
        std::cerr << "Compilation failed due to type errors." << std::endl;
        return;
    }
    std::cout << "--- Type checks passed! ---\n\n";

    // 3. LLVM Code Generator (Backend)
    angara::LLVMCodeGenerator generator(typeChecker, errorHandler);
    std::unique_ptr<llvm::Module> module = generator.generate(statements);

    if (!module) {
        std::cerr << "Compilation failed during LLVM IR generation." << std::endl;
        return;
    }

    // --- THIS IS OUR "DISASSEMBLER" ---
    // Print the generated LLVM IR to the console.
    std::cout << "--- Generated LLVM IR ---\n";
    module->print(llvm::outs(), nullptr);
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

    compile(source);

    return 0;
}