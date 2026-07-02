#include "CLI.h"
#include "Colors.h"
#include "Platform.h"
#include <iostream>
#include <filesystem>
#include <set>
namespace fs = std::filesystem;
namespace angara {

int angara::CLI::handleTest(std::vector<std::string> args) {
    args.erase(args.begin());
    parseFlags(args); CliFlags& flags = m_flags;

    // Find test directory
    std::string test_dir = "tests";
    if (!args.empty() && fs::is_directory(args[0])) {
        test_dir = args[0];
    }

    if (!fs::is_directory(test_dir)) {
        std::cerr << CLR_RED << "[ERROR] Test directory '" << test_dir << "' not found." << CLR_RESET << "\n";
        return 1;
    }

    std::string angc_bin = "build/angc";

    int pass = 0, fail = 0;
    std::vector<std::string> bugs;

    std::cout << "\n" << CLR_BOLD << CLR_CYAN << "Angara Test Runner" << CLR_RESET << "\n\n";

    // Run positive tests (should compile & run successfully)
    std::string pos_dir = test_dir + "/positive";
    if (fs::is_directory(pos_dir)) {
        std::cout << CLR_BOLD << CLR_CYAN << "-- Positive Tests (should compile & run) --" << CLR_RESET << "\n\n";
        for (const auto& entry : fs::directory_iterator(pos_dir)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".an") continue;
            std::string test_name = entry.path().stem().string();
            std::string test_file = entry.path().string();

            std::cout << "  " << CLR_BOLD << test_name << CLR_RESET << ": ";

            // Compile
            std::string binary = "/tmp/angara_test_" + test_name;
            std::string compile_cmd = angc_bin + " \"" + test_file + "\" -o \"" + binary + "\" 2>&1";
            FILE* pipe = popen(compile_cmd.c_str(), "r");
            if (!pipe) {
                std::cout << CLR_RED << "ERROR" << CLR_RESET << " (popen failed)\n";
                fail++;
                continue;
            }
            char buffer[4096];
            std::string compile_output;
            while (fgets(buffer, sizeof(buffer), pipe)) compile_output += buffer;
            int compile_rc = pclose(pipe);

            if (compile_rc != 0) {
                if (compile_output.find("SIGABRT") != std::string::npos ||
                    compile_output.find("SIGSEGV") != std::string::npos) {
                    std::cout << CLR_RED << "CRASH" << CLR_RESET << " (compiler crashed)\n";
                    bugs.push_back("BUG [" + test_name + "]: Compiler crash");
                } else if (compile_output.find("Linker") != std::string::npos ||
                           compile_output.find("Undefined") != std::string::npos) {
                    std::cout << CLR_RED << "LINKER ERROR" << CLR_RESET << "\n";
                    bugs.push_back("BUG [" + test_name + "]: Linker error");
                } else {
                    std::cout << CLR_RED << "COMPILE ERROR" << CLR_RESET << "\n";
                    bugs.push_back("BUG [" + test_name + "]: Unexpected compile error");
                }
                fail++;
                continue;
            }

            // Run
            if (!fs::exists(binary)) {
                std::cout << CLR_RED << "NO BINARY" << CLR_RESET << "\n";
                fail++;
                continue;
            }

            std::string run_cmd = "\"" + binary + "\" 2>&1";
            pipe = popen(run_cmd.c_str(), "r");
            if (!pipe) {
                std::cout << CLR_RED << "ERROR" << CLR_RESET << " (popen failed)\n";
                fail++;
                continue;
            }
            std::string run_output;
            while (fgets(buffer, sizeof(buffer), pipe)) run_output += buffer;
            int run_rc = pclose(pipe);
            fs::remove(binary);

            if (run_rc != 0) {
                if (run_rc == 139) {
                    std::cout << CLR_RED << "SEGFAULT" << CLR_RESET << " (runtime)\n";
                    bugs.push_back("BUG [" + test_name + "]: Runtime segfault");
                } else if (run_rc == 134) {
                    std::cout << CLR_RED << "ABORT" << CLR_RESET << " (runtime)\n";
                    bugs.push_back("BUG [" + test_name + "]: Runtime abort");
                } else {
                    std::cout << CLR_RED << "RUNTIME ERROR" << CLR_RESET << " (exit " << (run_rc >> 8) << ")\n";
                    bugs.push_back("BUG [" + test_name + "]: Runtime error");
                }
                fail++;
                continue;
            }

            std::cout << CLR_BOLD << CLR_GREEN << "PASS" << CLR_RESET << "\n";
            pass++;
        }
    }

    // Run negative tests (should produce compilation errors)
    std::string neg_dir = test_dir + "/negative";
    if (fs::is_directory(neg_dir)) {
        std::cout << "\n" << CLR_BOLD << CLR_CYAN << "-- Negative Tests (should fail to compile) --" << CLR_RESET << "\n\n";
        for (const auto& entry : fs::directory_iterator(neg_dir)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".an") continue;
            std::string test_name = entry.path().stem().string();
            std::string test_file = entry.path().string();

            std::cout << "  " << CLR_BOLD << test_name << CLR_RESET << ": ";

            std::string binary = "/tmp/angara_test_neg_" + test_name;
            std::string compile_cmd = angc_bin + " \"" + test_file + "\" -o \"" + binary + "\" 2>&1";
            FILE* pipe = popen(compile_cmd.c_str(), "r");
            if (!pipe) {
                std::cout << CLR_RED << "ERROR" << CLR_RESET << " (popen failed)\n";
                fail++;
                continue;
            }
            char buffer[4096];
            std::string compile_output;
            while (fgets(buffer, sizeof(buffer), pipe)) compile_output += buffer;
            int compile_rc = pclose(pipe);

            if (compile_rc == 0) {
                std::cout << CLR_RED << "MISSING ERROR" << CLR_RESET << " (compiled but should have failed)\n";
                bugs.push_back("BUG [" + test_name + "]: Failed to catch error");
                fail++;
                continue;
            }

            if (compile_output.find("SIGABRT") != std::string::npos ||
                compile_output.find("SIGSEGV") != std::string::npos) {
                std::cout << CLR_RED << "CRASH" << CLR_RESET << " (crashed instead of error)\n";
                bugs.push_back("BUG [" + test_name + "]: Crash instead of error");
                fail++;
            } else {
                // Extract the first error line
                std::string error_line;
                std::istringstream iss(compile_output);
                std::string line;
                while (std::getline(iss, line)) {
                    if (line.find("Error") != std::string::npos || line.find("error") != std::string::npos) {
                        // Strip ANSI
                        std::string clean;
                        for (char c : line) {
                            if (c == '\033') { while (line.find('m', line.find(c)) != std::string::npos) break; continue; }
                            clean += c;
                        }
                        error_line = clean;
                        break;
                    }
                }
                if (!error_line.empty()) {
                    std::cout << CLR_BOLD << CLR_GREEN << "CAUGHT" << CLR_RESET << " " << error_line << "\n";
                } else {
                    std::cout << CLR_BOLD << CLR_GREEN << "CAUGHT" << CLR_RESET << "\n";
                }
                pass++;
            }
        }
    }

    // Print summary
    int total = pass + fail;
    std::cout << "\n" << CLR_BOLD << "Results: " << CLR_GREEN << pass << " passed" << CLR_RESET;
    if (fail > 0) {
        std::cout << ", " << CLR_RED << fail << " failed" << CLR_RESET;
    }
    std::cout << " out of " << total << " tests\n";

    if (!bugs.empty()) {
        std::cout << "\n" << CLR_BOLD << CLR_YELLOW << "Bug Report:" << CLR_RESET << "\n";
        for (size_t i = 0; i < bugs.size(); i++) {
            std::cout << "  " << CLR_YELLOW << (i + 1) << ". " << bugs[i] << CLR_RESET << "\n";
        }
    }

    std::cout << "\n";
    return fail;
}
} // namespace angara
