#include "CLI.h"
#include "Colors.h"
#include <iostream>
#include <map>
namespace angara {

int angara::CLI::handleExplain(std::vector<std::string> args) {
    if (args.empty()) {
        std::cerr << CLR_RED << "[ERROR] 'explain' requires an error or warning code (e.g., W003, E377)." << CLR_RESET << "\n";
        return 1;
    }
    std::string code = args[0];

    // Warning explanations
    static const std::map<std::string, std::pair<std::string, std::string>> explanations = {
        {"W001", {"Division or modulo by zero", "The compiler detected that the divisor in a division or modulo operation is the constant zero.\n\n  Example:\n    let x = 10 / 0;    // W001\n    let y = 10 % 0;    // W001\n\n  Fix: Ensure the divisor is non-zero. If this is intentional in a\ngeneric context, suppress with -Wno-W001."}},
        {"W002", {"Using 'nil' in a logical expression", "The compiler detected 'nil' used as an operand of '&&' or '||'. Since nil is falsy, 'nil && x' always evaluates to nil and 'nil || x' always evaluates to x.\n\n  Example:\n    let result = nil && true;    // W002: always nil\n\n  Fix: Check if you meant to use an optional check instead, or\nsuppress with -Wno-W002 if this is intentional."}},
        {"W003", {"Unused variable", "A variable was declared but never read before going out of scope.\n\n  Example:\n    func foo() {\n        let x = 42;    // W003: 'x' is never used\n    }\n\n  Fix: Either use the variable, prefix it with '_' to signal\nintentional discard ('_x'), or remove the declaration entirely.\nSuppress with -Wno-W003."}},
    };

    auto it = explanations.find(code);
    if (it != explanations.end()) {
        std::cout << CLR_BOLD << CLR_CYAN << code << CLR_RESET << ": " << it->second.first << "\n\n";
        std::cout << it->second.second << "\n";
    } else if (code[0] == 'W') {
        std::cout << CLR_BOLD << CLR_YELLOW << code << CLR_RESET << " is a compiler warning.\n";
        std::cout << CLR_DIM << "  Suppress with: -Wno-" << code << "\n" << CLR_RESET;
        std::cout << "  No detailed explanation is available for this code yet.\n";
    } else if (code[0] == 'E') {
        std::cout << CLR_BOLD << CLR_RED << code << CLR_RESET << " is a compiler error.\n";
        std::cout << "  No detailed explanation is available for this code yet.\n";
        std::cout << CLR_DIM << "  The error message printed by the compiler should describe the issue.\n" << CLR_RESET;
    } else {
        std::cerr << CLR_RED << "[ERROR] Unknown code format: " << code << CLR_RESET << "\n";
        std::cerr << "  Expected a code like W003 or E377.\n";
        return 1;
    }
    return 0;
}
} // namespace angara
