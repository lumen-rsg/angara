#include "CLI.h"
#include "BuildSystem.h"
#include "Colors.h"
#include "Platform.h"
#include <iostream>
#include <filesystem>
#include <thread>
#include <chrono>
namespace fs = std::filesystem;
namespace angara {

static std::vector<fs::path> collect_an_files(const fs::path& root) {
    std::vector<fs::path> files;
    try {
        for (const auto& entry : fs::recursive_directory_iterator(root)) {
            if (entry.is_regular_file() && entry.path().extension() == ".an") {
                files.push_back(entry.path());
            }
        }
    } catch (...) {}
    return files;
}

struct FileSnapshot {
    fs::path path;
    fs::file_time_type last_write;
};

static std::vector<FileSnapshot> snapshot_files(const std::vector<fs::path>& files) {
    std::vector<FileSnapshot> snap;
    for (const auto& f : files) {
        try {
            snap.push_back({f, fs::last_write_time(f)});
        } catch (...) {}
    }
    return snap;
}

static bool detect_changes(const std::vector<FileSnapshot>& prev,
                           std::vector<FileSnapshot>& curr) {
    bool changed = false;
    for (size_t i = 0; i < curr.size(); i++) {
        if (i < prev.size() && curr[i].path == prev[i].path) {
            if (curr[i].last_write != prev[i].last_write) {
                changed = true;
                curr[i] = {curr[i].path, fs::last_write_time(curr[i].path)};
            }
        } else {
            changed = true;
        }
    }
    // Check for new files
    try {
        for (const auto& entry : fs::recursive_directory_iterator(".")) {
            if (entry.is_regular_file() && entry.path().extension() == ".an") {
                bool found = false;
                for (auto& c : curr) {
                    if (c.path == entry.path()) { found = true; break; }
                }
                if (!found) {
                    changed = true;
                    curr.push_back({entry.path(), fs::last_write_time(entry.path())});
                }
            }
        }
    } catch (...) {}
    return changed;
}

int angara::CLI::handleWatch(std::vector<std::string> args) {
    bool run_after = false;
    bool test_after = false;
    for (auto& arg : args) {
        if (arg == "-r" || arg == "--run") run_after = true;
        else if (arg == "-t" || arg == "--test") test_after = true;
    }

    std::string project_file = find_local_project_file();
    if (project_file.empty()) {
        std::cerr << CLR_RED << "[ERROR] No .abs project file found in the current directory." << CLR_RESET << "\n";
        return 1;
    }

    std::cout << CLR_BOLD << CLR_CYAN << "[WATCH] " << CLR_RESET
              << "Watching for changes (project: " << project_file << ")\n";
    if (run_after) std::cout << CLR_DIM << "  --run enabled: will execute after each build\n" << CLR_RESET;
    if (test_after) std::cout << CLR_DIM << "  --test enabled: will run tests after each build\n" << CLR_RESET;
    std::cout << CLR_DIM << "  Press Ctrl+C to stop\n" << CLR_RESET << "\n";

    auto an_files = collect_an_files(".");
    auto snapshot = snapshot_files(an_files);

    // Initial build
    {
        auto t0 = std::chrono::high_resolution_clock::now();
        std::cout << CLR_BOLD << CLR_CYAN << "[WATCH] " << CLR_RESET << "Initial build...\n";
        angara::BuildSystem builder;
        bool ok = builder.build(project_file);
        auto t1 = std::chrono::high_resolution_clock::now();
        double secs = std::chrono::duration<double>(t1 - t0).count();

        if (ok) {
            std::cout << CLR_BOLD << CLR_GREEN << "[WATCH] " << CLR_RESET
                      << "Build OK" << CLR_DIM << " (" << secs << "s)" << CLR_RESET << "\n";
            if (run_after) builder.run(project_file);
            if (test_after) {
                std::vector<std::string> test_args = {"test"};
                handleTest(test_args);
            }
        } else {
            std::cout << CLR_BOLD << CLR_RED << "[WATCH] " << CLR_RESET << "Build failed\n";
        }
        std::cout << CLR_DIM << "  Waiting for changes...\n" << CLR_RESET;
    }

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        auto current_files = collect_an_files(".");
        auto current_snap = snapshot_files(current_files);
        if (!detect_changes(snapshot, current_snap)) continue;
        snapshot = current_snap;

        // Debounce: wait for changes to settle
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        current_snap = snapshot_files(collect_an_files("."));
        snapshot = current_snap;

        auto t0 = std::chrono::high_resolution_clock::now();
        std::cout << "\n" << CLR_BOLD << CLR_CYAN << "[WATCH] " << CLR_RESET
                  << "Change detected, rebuilding...\n";

        angara::BuildSystem builder;
        bool ok = builder.build(project_file);
        auto t1 = std::chrono::high_resolution_clock::now();
        double secs = std::chrono::duration<double>(t1 - t0).count();

        if (ok) {
            std::cout << CLR_BOLD << CLR_GREEN << "[WATCH] " << CLR_RESET
                      << "Build OK" << CLR_DIM << " (" << secs << "s)" << CLR_RESET << "\n";
            if (run_after) builder.run(project_file);
            if (test_after) {
                std::vector<std::string> test_args = {"test"};
                handleTest(test_args);
            }
        } else {
            std::cout << CLR_BOLD << CLR_RED << "[WATCH] " << CLR_RESET << "Build failed\n";
        }
        std::cout << CLR_DIM << "  Waiting for changes...\n" << CLR_RESET;
    }
    return 0;
}
} // namespace angara
