#include "CLI.h"
#include "Platform.h"
#include <filesystem>
namespace fs = std::filesystem;
namespace angara {
std::string get_host_os_triple_suffix() {
#if defined(__APPLE__)
    return "-apple-darwin";
#elif defined(__linux__)
    return "-unknown-linux-gnu";
#elif defined(_WIN32)
    return "-pc-windows-msvc";
#else
    return "-unknown-elf";
#endif
}

std::string resolve_target_triple(const std::string& input) {
    if (input.find('-') != std::string::npos) {
        return input;
    }

    const std::string os_suffix = get_host_os_triple_suffix();

    if (input == "arm64" || input == "aarch64") {
        return "aarch64" + os_suffix;
    }
    if (input == "amd64" || input == "x86_64" || input == "x64") {
        return "x86_64" + os_suffix;
    }
    if (input == "riscv64") {
        return "riscv64" + os_suffix;
    }
    if (input == "wasm32") {
        return "wasm32-unknown-unknown";
    }
    if (input == "wasm64") {
        return "wasm64-unknown-unknown";
    }

    return input;
}

std::string find_local_project_file() {
    try {
        for (const auto& entry : fs::directory_iterator(".")) {
            if (entry.is_regular_file() && entry.path().extension() == ".abs") {
                return entry.path().string();
            }
        }
    } catch (const std::exception& e) {
        // Silently ignore — find_local_project_file returning "" is valid
    } catch (...) {
        // Silently ignore
    }
    return "";
}

bool is_an_file(const std::string& s) {
    return s.length() >= 3 && s.substr(s.length() - 3) == ".an";
}
} // namespace angara
