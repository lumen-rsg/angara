#include <iostream>
#include <random>
#include <string>
#include <thread>

#include "CompilerDriver.h"

// --- Standard Color Constants ---
const auto RESET = "\033[0m";
const auto BOLD = "\033[1m";
const auto RED = "\033[31m";
const auto GREEN = "\033[32m";
const auto YELLOW = "\033[33m";
const auto BLUE = "\033[34m";
const auto PURPLE = "\033[35m";
const auto CYAN = "\033[36m";

const std::string ANGC_VERSION = "2.0-staging";
const std::string ANGARA_SPEC = "v2-static";
const std::string BACKEND_VERSION = "2.3-staging";

void print_and_remove(const std::string& data) {
    std::random_device dev;
    std::mt19937 rng(dev());
    std::uniform_int_distribution<std::mt19937::result_type> dist6(40,100);

    for (const char c : data) {
        std::cout << BOLD << CYAN << c << RESET;
        std::cout << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(dist6(rng)));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    for (int i = 0; i < data.size(); i++) {
        std::cout << "\b \b";

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        std::cout << std::flush;
    }

    std::cout << "\r\033[K" << std::flush;
}

void print_easter() {
    std::cout << BOLD << CYAN << "-> analyzing request" << RESET << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
        std::cout << "\r\033[K";

        std::cout << BOLD << CYAN << "-> analyzing request." << RESET << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
        std::cout << "\r\033[K";

        std::cout << BOLD << CYAN << "-> analyzing request.." << RESET << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
        std::cout << "\r\033[K";

        std::cout << BOLD << CYAN << "-> analyzing request..." << RESET << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
        std::cout << "\r\033[K";

        print_and_remove("of course. perfection is the goal.");
        std::this_thread::sleep_for(std::chrono::milliseconds(450));
        print_and_remove("the craft of a creator is a journey, not a destination.");
        std::this_thread::sleep_for(std::chrono::milliseconds(360));
        print_and_remove("every line of code is a step. let's keep walking the path together.");
        std::this_thread::sleep_for(std::chrono::milliseconds(360));

        std::cout << "\033[38;5;27m" << "⠄⠄⠄⢰⣧⣼⣯⠄⣸⣠⣶⣶⣦⣾⠄⠄⠄⠄⡀⠄⢀⣿⣿⠄⠄⠄⢸⡇⠄⠄" << " : "  << RESET << PURPLE << R"(  ____ _____  ____ _____ __________ _)" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        std::cout << "\033[38;5;33m" << "⠄⠄⠄⣾⣿⠿⠿⠶⠿⢿⣿⣿⣿⣿⣦⣤⣄⢀⡅⢠⣾⣛⡉⠄⠄⠄⠸⢀⣿⠄" << " : "  << RESET << PURPLE << R"( / __ `/ __ \/ __ `/ __ `/ ___/ __ `/)" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        std::cout << "\033[38;5;39m" << "⠄⠄⢀⡋⣡⣴⣶⣶⡀⠄⠄⠙⢿⣿⣿⣿⣿⣿⣴⣿⣿⣿⢃⣤⣄⣀⣥⣿⣿⠄" << " : "  << RESET << PURPLE << R"(/ /_/ / / / / /_/ / /_/ / /  / /_/ / )" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        std::cout << "\033[38;5;69m" << "⠄⠄⢸⣇⠻⣿⣿⣿⣧⣀⢀⣠⡌⢻⣿⣿⣿⣿⣿⣿⣿⣿⣿⠿⠿⠿⣿⣿⣿⠄" << " : "  << RESET << PURPLE << R"(\__,_/_/ /_/\__, /\__,_/_/   \__,_/  )" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        std::cout << "\033[38;5;105m" << "⠄⢀⢸⣿⣷⣤⣤⣤⣬⣙⣛⢿⣿⣿⣿⣿⣿⣿⡿⣿⣿⡍⠄⠄⢀⣤⣄⠉⠋⣰" << " : " << RESET << PURPLE << R"(           /____/                    )" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        std::cout << "\033[38;5;141m" << "⠄⣼⣖⣿⣿⣿⣿⣿⣿⣿⣿⣿⢿⣿⣿⣿⣿⣿⢇⣿⣿⡷⠶⠶⢿⣿⣿⠇⢀⣤" << " : " << RESET << PURPLE << R"(->     lumina-labs : 2025 : <3     <-)" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

}


int main(const int argc, char* argv[]) {
    // 1. Handle command-line arguments.
    if (argc != 2) {
        std::cerr << RED << BOLD << "Error: " << RESET << "Incorrect usage." << std::endl;
        std::cerr << "Usage: angc <source_file.an | -v | --version>" << std::endl;
        return 1;
    }

    const std::string arg = argv[1];

    // 2. Check for the version flag.
    if (arg == "-v" || arg == "--version") {
        std::cout << GREEN << BOLD << "angc: Angara Compiler " << RESET << std::endl;
        std::cout << CYAN << "  -> compiler version: " << RESET << BOLD << ANGC_VERSION << RESET << std::endl;
        std::cout << CYAN << "  -> backend  version: " << RESET << BOLD << BACKEND_VERSION << RESET << std::endl;
        std::cout << CYAN << "  -> language version: " << RESET << BOLD << ANGARA_SPEC << RESET << std::endl;
        std::cout << std::endl;

        std::cout << YELLOW << BOLD << "this is a staging build. report bugs at https://github.com/lumen-rsg/angara " << RESET << std::endl;
        return 0; // Exit successfully after printing the version.
    }

    if (arg == "--make-perfect") {
        print_easter();
        return 0; // Exit successfully.
    }

    std::cout << "\033[38;5;27m"  << "⠄⠄⠄⢰⣧⣼⣯⠄⣸⣠⣶⣶⣦⣾⠄⠄⠄⠄⡀⠄⢀⣿⣿⠄⠄⠄⢸⡇⠄⠄" << "   "  << RESET << PURPLE << R"(  ____ _____  ____ _____ __________ _)" << std::endl;
    std::cout << "\033[38;5;33m"  << "⠄⠄⠄⣾⣿⠿⠿⠶⠿⢿⣿⣿⣿⣿⣦⣤⣄⢀⡅⢠⣾⣛⡉⠄⠄⠄⠸⢀⣿⠄" << "   "  << RESET << PURPLE << R"( / __ `/ __ \/ __ `/ __ `/ ___/ __ `/)" << std::endl;
    std::cout << "\033[38;5;39m"  << "⠄⠄⢀⡋⣡⣴⣶⣶⡀⠄⠄⠙⢿⣿⣿⣿⣿⣿⣴⣿⣿⣿⢃⣤⣄⣀⣥⣿⣿⠄" << "   "  << RESET << PURPLE << R"(/ /_/ / / / / /_/ / /_/ / /  / /_/ / )" << std::endl;
    std::cout << "\033[38;5;69m"  << "⠄⠄⢸⣇⠻⣿⣿⣿⣧⣀⢀⣠⡌⢻⣿⣿⣿⣿⣿⣿⣿⣿⣿⠿⠿⠿⣿⣿⣿⠄" << "   "  << RESET << PURPLE << R"(\__,_/_/ /_/\__, /\__,_/_/   \__,_/  )" << std::endl;
    std::cout << "\033[38;5;105m" << "⠄⢀⢸⣿⣷⣤⣤⣤⣬⣙⣛⢿⣿⣿⣿⣿⣿⣿⡿⣿⣿⡍⠄⠄⢀⣤⣄⠉⠋⣰" << "   " << RESET << PURPLE << R"(           /____/                    )" << std::endl;
    std::cout << "\033[38;5;141m" << "⠄⣼⣖⣿⣿⣿⣿⣿⣿⣿⣿⣿⢿⣿⣿⣿⣿⣿⢇⣿⣿⡷⠶⠶⢿⣿⣿⠇⢀⣤" << "   " << RESET << PURPLE << R"(->       angc v2.4 | spec 2.2      <-)" << std::endl;
    std::cout << RESET << std::endl;

    // 3. If it's not a version flag, proceed with compilation.

    // 4. Return the appropriate final exit code.
    if (angara::CompilerDriver driver; driver.compile(arg)) {
        std::cout << "\n" << BOLD << GREEN << "Build successful." << RESET << std::endl;
        return 0;
    } else {
        std::cout << "\n" << BOLD << RED << "Build failed." << RESET << std::endl;
        return 1;
    }
}