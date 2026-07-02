#include "CLI.h"
#include <fstream>
#include "Formatter.h"
#include "Lexer.h"
#include "Parser.h"
#include "ErrorHandler.h"
#include "Colors.h"
#include <iostream>
#include <filesystem>
namespace fs = std::filesystem;
namespace angara {

int angara::CLI::handleFmt(std::vector<std::string> args) {
    args.erase(args.begin());
    bool write_in_place = false;
    bool check_mode = false;
    bool list_mode = false;
    std::vector<std::string> files;
    for (auto& arg : args) {
        if (arg == "-w" || arg == "--write") {
            write_in_place = true;
        } else if (arg == "-c" || arg == "--check") {
            check_mode = true;
        } else if (arg == "-l" || arg == "--list") {
            list_mode = true;
        } else {
            files.push_back(arg);
        }
    }

    if (files.empty()) {
        std::cerr << CLR_RED << "[ERROR] 'fmt' requires at least one .an source file." << CLR_RESET << "\n";
        std::cerr << "Usage: angc fmt [-w|--write] [-c|--check] [-l|--list] <file.an> [file2.an ...]\n";
        return 1;
    }

    int errors = 0;
    int needs_formatting = 0;
    for (auto& file : files) {
        std::ifstream ifs(file);
        if (!ifs.is_open()) {
            std::cerr << CLR_RED << "[ERROR] Cannot open file: " << file << CLR_RESET << "\n";
            errors++;
            continue;
        }
        std::string source((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        ifs.close();

        auto filename_ptr = std::make_shared<std::string>(file);
        angara::ErrorHandler errorHandler(source);
        angara::Lexer lexer(source, filename_ptr, errorHandler);
        auto tokens = lexer.scanTokens();
        if (errorHandler.hadError()) { errors++; continue; }

        angara::Parser parser(tokens, errorHandler);
        auto statements = parser.parseStmts();
        if (errorHandler.hadError()) { errors++; continue; }

        angara::Formatter formatter;
        std::string formatted = formatter.format(statements);

        if (check_mode || list_mode) {
            if (formatted != source) {
                needs_formatting++;
                if (list_mode) {
                    std::cout << file << "\n";
                } else {
                    std::cerr << CLR_RED << "[FAIL] " << CLR_RESET << file << " needs formatting\n";
                }
            } else if (check_mode && !list_mode) {
                std::cout << CLR_BOLD << CLR_GREEN << "[OK] " << CLR_RESET << file << "\n";
            }
        } else if (write_in_place) {
            std::ofstream ofs(file);
            ofs << formatted;
            ofs.close();
            std::cout << CLR_BOLD << CLR_GREEN << "[OK] " << CLR_RESET << "Formatted: " << file << "\n";
        } else {
            std::cout << formatted;
        }
    }
    if (errors > 0) return 1;
    if ((check_mode || list_mode) && needs_formatting > 0) return 1;
    return 0;
}
} // namespace angara
