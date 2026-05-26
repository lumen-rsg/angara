#pragma once
#include <string>
#include <vector>

namespace angara {
    /// Interactive and template-based project initializer.
    /// Creates a .abs build specification and starter source files.
    class ProjectInitializer {
    public:
        /// Runs the initializer with the given template, or launches interactive mode if empty.
        /// @param template_name  One of: "app", "lib", "embedded", "gui", or "" for interactive.
        /// @return True if the project was created successfully.
        static bool run(const std::string& template_name = "");
    private:
        /// Prompts the user for input with an optional default value.
        /// @param label        The prompt text.
        /// @param defaultValue  The value returned if the user presses Enter.
        /// @return The user's input or the default.
        static std::string prompt(const std::string& label, const std::string& defaultValue = "");

        /// Prompts the user for a yes/no confirmation.
        /// @param label  The question to ask.
        /// @return True if the user answered yes.
        static bool confirm(const std::string& label);

        /// Creates a console application project with io dependency.
        static bool init_app(const std::string& name, const std::string& author);

        /// Creates a library project with an example exported function.
        static bool init_lib(const std::string& name, const std::string& author);

        /// Creates a bare-metal/freestanding project with UART example and linker script.
        static bool init_embedded(const std::string& name, const std::string& author);

        /// Creates a GUI project with Dear ImGui native module configuration.
        static bool init_gui(const std::string& name, const std::string& author);

        /// Launches a fully interactive questionnaire to build the .abs file.
        static bool init_interactive();
    };
}
