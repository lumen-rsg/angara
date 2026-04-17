#pragma once
#include <string>
#include <vector>

namespace angara {
    class ProjectInitializer {
    public:
        // template_name can be: "", "app", "lib", "embedded", "gui"
        static bool run(const std::string& template_name = "");
    private:
        static std::string prompt(const std::string& label, const std::string& defaultValue = "");
        static bool confirm(const std::string& label);

        // Template generators
        static bool init_app(const std::string& name, const std::string& author);
        static bool init_lib(const std::string& name, const std::string& author);
        static bool init_embedded(const std::string& name, const std::string& author);
        static bool init_gui(const std::string& name, const std::string& author);
        static bool init_interactive();
    };
}
