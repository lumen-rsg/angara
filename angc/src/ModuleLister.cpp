#include "CLI.h"
#include "Colors.h"
#include "Platform.h"
#include <iostream>
#include <filesystem>
namespace fs = std::filesystem;
namespace angara {
void list_modules() {
    const std::string mod_path = "/opt/angara/modules";
    std::cout << CLR_BOLD << "Installed Native Modules:" << CLR_RESET << "\n";
    std::cout << CLR_GRAY << "  Path: " << mod_path << CLR_RESET << "\n\n";

    int count = 0;
    try {
        for (const auto& entry : fs::directory_iterator(mod_path)) {
            if (!entry.is_regular_file()) continue;
            std::string name = entry.path().filename().string();
            if (name.rfind("lib") == 0) name = name.substr(3);
            auto ext = entry.path().extension().string();
            if (ext == ".dylib" || ext == ".so" || ext == ".dll") {
                name = name.substr(0, name.size() - ext.size());
                std::cout << CLR_GREEN << "  • " << CLR_RESET << name
                          << CLR_GRAY << " (" << entry.path().filename().string() << ")" << CLR_RESET << "\n";
                count++;
            }
        }
    } catch (const std::exception& e) {
        std::cout << CLR_YELLOW << "  (module directory not found: " << e.what() << ")" << CLR_RESET << "\n";
    }

    if (count == 0) {
        std::cout << CLR_YELLOW << "  No modules installed." << CLR_RESET << "\n";
    }
    std::cout << "\n";
}
} // namespace angara
