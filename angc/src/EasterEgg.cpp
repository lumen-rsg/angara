//
//  EasterEgg.cpp — angc --make-perfect
//  Because compilers deserve personality.
//

#include "EasterEgg.h"
#include "Colors.h"
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <sstream>

namespace angara {

// ─── Helpers ───────────────────────────────────────────────────────────

static void sleep_ms(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

static void clear_line() {
    std::cout << "\r\033[2K" << std::flush;
}

static void print_right_ok() {
    std::cout << "\033[60G" << CLR_BOLD << CLR_GREEN << "[OK]" << CLR_RESET << "\n" << std::flush;
}

static void print_right_fail_then_ok() {
    std::cout << "\033[60G" << CLR_BOLD << CLR_RED << "[FAIL]" << CLR_RESET;
    sleep_ms(600);
    std::cout << "\r\033[60G" << CLR_BOLD << CLR_YELLOW << "[WARN]" << CLR_RESET << "\n" << std::flush;
}

// ─── Act 1: Fake BIOS Boot ────────────────────────────────────────────

static void act_bios_boot() {
    std::cout << CLR_BOLD << CLR_CYAN << "\nANGC BIOS v3.0.0" << CLR_RESET << " — Initializing...\n\n";
    sleep_ms(300);

    const std::vector<std::pair<std::string, bool>> checks = {
        {"Detecting CPU...        Angara Virtual Machine @ 4.2 THz", true},
        {"Loading types...        [i64] [f64] [string] [bool] [nil]", true},
        {"Mounting /dev/null...   Segfault prevention enabled", true},
        {"Checking memory...      0xDEADBEEF", true},
        {"Scanning for bugs...    ", false},
        {"Loading stdlib...       io, json, http, fs, time", true},
        {"Initializing LLVM...    Optimization level: overkill", true},
    };

    for (const auto& [msg, ok] : checks) {
        std::cout << "  " << CLR_DIM << msg << CLR_RESET << std::flush;
        sleep_ms(250 + rand() % 200);
        if (ok) {
            print_right_ok();
        } else {
            print_right_fail_then_ok();
            sleep_ms(200);
            std::cout << "  " << CLR_DIM << "  ↳ Retrying with coffee...  " << CLR_RESET << std::flush;
            sleep_ms(400);
            print_right_ok();
        }
    }

    std::cout << "\n";
    sleep_ms(200);
}

// ─── Act 2: Funny Compilation Progress ────────────────────────────────

static void act_compilation_progress() {
    const std::vector<std::string> phases = {
        "Tokenizing coffee",
        "Parsing developer intentions",
        "Resolving existential types",
        "Type-checking vibes",
        "Optimizing procrastination",
        "Inlining happiness",
        "Dead-code eliminating sadness",
        "Linking thoughts to reality",
        "Generating existential LLVM IR",
        "Writing bitcode to /dev/dreams"
    };

    const int bar_width = 30;

    std::cout << CLR_BOLD << CLR_CYAN << "  Compiling perfection:" << CLR_RESET << "\n\n";

    for (size_t i = 0; i < phases.size(); ++i) {
        float progress = static_cast<float>(i + 1) / phases.size();
        int filled = static_cast<int>(bar_width * progress);

        std::stringstream bar;
        bar << CLR_BOLD << CLR_GREEN << "[";
        for (int j = 0; j < bar_width; ++j) {
            if (j < filled) bar << "\xe2\x96\x88";
            else bar << "\xe2\x96\x91";
        }
        bar << "]" << CLR_RESET;

        const char spinner[] = "|/-\\";
        char spin = spinner[i % 4];

        int pct = static_cast<int>(progress * 100);

        std::cout << "\r  " << CLR_DIM << spin << CLR_RESET
                  << " " << phases[i]
                  << " " << bar.str()
                  << " " << CLR_BOLD << CLR_CYAN << pct << "%" << CLR_RESET
                  << "  " << std::flush;

        sleep_ms(300 + rand() % 400);
    }

    std::cout << "\r\033[2K";
    std::cout << "  " << CLR_BOLD << CLR_GREEN
              << "\xe2\x9c\x93 Compilation complete. 0 errors, 0 warnings, 1 existential crisis resolved."
              << CLR_RESET << "\n\n";
    sleep_ms(400);
}

// ─── Act 3: Epic ASCII Art Banner with Reveal ─────────────────────────

static void act_banner_reveal() {
    const std::vector<std::string> banner = {
        "     _    _ _   _ _   _ ___  ___ ___  ___ ___  ___",
        "    / \\  | | \\ | | | | / __|| _ \\ _ \\/ __/ __|| _ \\",
        "   / _ \\ | |  \\| | | | \\__ \\|   /|   / (__\\__ \\|  _/",
        "  /_/ \\_\\|_|_|\\__|_|_|_|___/|_|_\\_|_\\\\___|___/|_|  ",
    };

    // Reveal line by line with a sweep effect
    for (const auto& line : banner) {
        std::cout << CLR_BOLD << CLR_MAGENTA;
        for (char c : line) {
            std::cout << c << std::flush;
            sleep_ms(2);
        }
        std::cout << CLR_RESET << "\n";
    }

    // Flash the banner in cyan then back to magenta
    sleep_ms(200);
    std::cout << "\033[4A"; // Move up 4 lines
    for (const auto& line : banner) {
        std::cout << "\r" << CLR_BOLD << CLR_CYAN << line << CLR_RESET << "\n";
    }
    sleep_ms(100);
    std::cout << "\033[4A";
    for (const auto& line : banner) {
        std::cout << "\r" << CLR_BOLD << CLR_MAGENTA << line << CLR_RESET << "\n";
    }

    sleep_ms(300);
}

// ─── Act 4: Random Wisdom ─────────────────────────────────────────────

static void act_wisdom() {
    const std::vector<std::string> quotes = {
        "The craft of code is the craft of thought.",
        "It compiles! (tm) Ship it.",
        "segfault is just the CPU's way of saying 'surprise'.",
        "In Angara we trust. In C we segfault.",
        "There are only 2 hard problems: cache invalidation, naming, and off-by-one errors.",
        "This compiler is 100% bug-free. (Terms and conditions apply.)",
        "404: productivity not found. Compiling instead...",
        "The best time to write code was yesterday. The second best is now.",
        "Why did the developer go broke? Because he used up all his cache.",
        "There is no place like 127.0.0.1",
    };

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, static_cast<int>(quotes.size()) - 1);
    const std::string& quote = quotes[dist(gen)];

    // Typing effect
    std::cout << "\n    " << CLR_BOLD << CLR_YELLOW << "\xc2\xbb " << CLR_RESET;
    for (char c : quote) {
        std::cout << CLR_BOLD << c << CLR_RESET << std::flush;
        sleep_ms(25);
    }
    std::cout << "\n\n";
    sleep_ms(500);
}

// ─── Act 5: CRT Turn-off Effect ───────────────────────────────────────

static void act_crt_off() {
    int width = 80;

    // Phase 1: Flash white
    for (int i = 0; i < 3; ++i) {
        std::cout << "\r" << CLR_BOLD << CLR_WHITE;
        for (int j = 0; j < width; ++j) std::cout << "\xe2\x96\x88";
        std::cout << CLR_RESET << std::flush;
        sleep_ms(40);
        clear_line();
        sleep_ms(30);
    }

    // Phase 2: Collapse to center line
    for (int half = width / 2; half > 2; half -= 3) {
        clear_line();
        int pad = (width / 2) - half;
        std::cout << "\r";
        for (int i = 0; i < pad; ++i) std::cout << " ";
        std::cout << CLR_BOLD << CLR_WHITE;
        for (int i = 0; i < half * 2; ++i) std::cout << "\xe2\x96\x88";
        std::cout << CLR_RESET << std::flush;
        sleep_ms(20);
    }

    // Phase 3: Shrink to a bright dot
    clear_line();
    for (int sz = 10; sz >= 1; --sz) {
        std::cout << "\r";
        int pad = (width / 2) - sz / 2;
        for (int i = 0; i < pad; ++i) std::cout << " ";
        std::cout << CLR_BOLD << CLR_WHITE;
        for (int i = 0; i < sz; ++i) std::cout << "\xe2\x96\x88";
        std::cout << CLR_RESET << std::flush;
        sleep_ms(40);
    }

    // Final dot fade
    for (int brightness = 5; brightness >= 0; --brightness) {
        clear_line();
        if (brightness > 0) {
            std::cout << "\r";
            for (int i = 0; i < width / 2; ++i) std::cout << " ";
            if (brightness > 3) std::cout << CLR_BOLD << CLR_WHITE << "\xe2\x97\x8f" << CLR_RESET;
            else if (brightness > 1) std::cout << CLR_DIM << "\xe2\x97\x8f" << CLR_RESET;
            else std::cout << CLR_DIM << "\xc2\xb7" << CLR_RESET;
            std::cout << std::flush;
        }
        sleep_ms(120);
    }

    sleep_ms(300);
    std::cout << "\n";
}

// ─── Main Entry ────────────────────────────────────────────────────────

void run_easter_egg() {
    act_bios_boot();
    act_compilation_progress();
    act_banner_reveal();
    act_wisdom();
    act_crt_off();
}

} // namespace angara