#pragma once
#include <string>
#include <vector>

namespace angara {
    class ProjectInitializer {
    public:
        static bool run();
    private:
        static std::string prompt(const std::string& label, const std::string& defaultValue = "");
        static bool confirm(const std::string& label);
    };
}