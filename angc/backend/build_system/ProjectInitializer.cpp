#include "ProjectInitializer.h"
#include "Colors.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <sstream>

namespace fs = std::filesystem;

namespace angara {

    std::string ProjectInitializer::prompt(const std::string& label, const std::string& defaultValue) {
        std::cout << CLR_BOLD << CLR_CYAN << "? " << CLR_RESET << label;
        if (!defaultValue.empty()) {
            std::cout << " (" << defaultValue << ")";
        }
        std::cout << ": ";

        std::string input;
        std::getline(std::cin, input);

        if (input.empty()) return defaultValue;
        return input;
    }

    bool ProjectInitializer::confirm(const std::string& label) {
        std::string res = prompt(label + " [y/N]", "n");
        std::transform(res.begin(), res.end(), res.begin(), ::tolower);
        return res == "y" || res == "yes";
    }

    // =====================================================
    // Main Entry Point
    // =====================================================

    bool ProjectInitializer::run(const std::string& template_name) {
        if (template_name.empty()) {
            return init_interactive();
        }

        // Derive name from current directory
        std::string name = fs::current_path().filename().string();
        std::string author = prompt("Author", "user");

        if (template_name == "app") return init_app(name, author);
        if (template_name == "lib" || template_name == "library") return init_lib(name, author);
        if (template_name == "embedded" || template_name == "bare") return init_embedded(name, author);
        if (template_name == "gui") return init_gui(name, author);

        std::cerr << CLR_RED << "Unknown template: " << template_name << CLR_RESET << "\n";
        std::cerr << CLR_GRAY << "Available templates: app, lib, embedded, gui" << CLR_RESET << "\n";
        return false;
    }

    // =====================================================
    // Template: App (default console application)
    // =====================================================

    bool ProjectInitializer::init_app(const std::string& name, const std::string& author) {
        std::cout << CLR_BOLD << CLR_MAGENTA << "--- Creating App Project: " << name << " ---" << CLR_RESET << "\n";

        // Create .abs
        std::ofstream abs_file("project.abs");
        abs_file << "[workspace]\n"
                 << "name = " << name << "\n"
                 << "author = " << author << "\n"
                 << "version = 0.1.0\n"
                 << "description = A new Angara application\n\n"
                 << "[project]\n"
                 << "name = " << name << "\n"
                 << "type = app\n"
                 << "entry = main.an\n"
                 << "dependencies = [io]\n\n"
                 << "[profile]\n"
                 << "mode = debug\n"
                 << "opt = 0\n";
        abs_file.close();

        // Create main.an
        if (!fs::exists("main.an")) {
            std::ofstream src("main.an");
            src << "attach io;\n\n"
                << "// " << name << " — Angara Application\n\n"
                << "export func main() -> i64 {\n"
                << "    io.println(1, \"Hello from " << name << "!\");\n\n"
                << "    return 0;\n"
                << "}\n";
            src.close();
        }

        std::cout << CLR_BOLD << CLR_GREEN << "✓ App project created." << CLR_RESET << "\n";
        std::cout << CLR_GRAY << "  Run with: angc run" << CLR_RESET << "\n";
        return true;
    }

    // =====================================================
    // Template: Library
    // =====================================================

    bool ProjectInitializer::init_lib(const std::string& name, const std::string& author) {
        std::cout << CLR_BOLD << CLR_MAGENTA << "--- Creating Library Project: " << name << " ---" << CLR_RESET << "\n";

        std::ofstream abs_file("project.abs");
        abs_file << "[workspace]\n"
                 << "name = " << name << "\n"
                 << "author = " << author << "\n"
                 << "version = 0.1.0\n"
                 << "description = An Angara library\n\n"
                 << "[project]\n"
                 << "name = " << name << "\n"
                 << "type = library\n"
                 << "entry = lib.an\n"
                 << "dependencies = []\n\n"
                 << "[profile]\n"
                 << "mode = debug\n"
                 << "opt = 0\n";
        abs_file.close();

        // Create lib.an
        if (!fs::exists("lib.an")) {
            std::ofstream src("lib.an");
            src << "// " << name << " — Angara Library\n\n"
                << "// Export a simple greeting function\n"
                << "export func greet(name: string) -> string {\n"
                << "    return \"Hello, \" + name + \"!\";\n"
                << "}\n";
            src.close();
        }

        std::cout << CLR_BOLD << CLR_GREEN << "✓ Library project created." << CLR_RESET << "\n";
        std::cout << CLR_GRAY << "  Build with: angc" << CLR_RESET << "\n";
        return true;
    }

    // =====================================================
    // Template: Embedded / Bare-metal
    // =====================================================

    bool ProjectInitializer::init_embedded(const std::string& name, const std::string& author) {
        std::cout << CLR_BOLD << CLR_MAGENTA << "--- Creating Embedded Project: " << name << " ---" << CLR_RESET << "\n";

        std::string target = prompt("Target triple (e.g., aarch64, arm64)", "aarch64");

        std::ofstream abs_file("project.abs");
        abs_file << "[workspace]\n"
                 << "name = " << name << "\n"
                 << "author = " << author << "\n"
                 << "version = 0.1.0\n"
                 << "description = Bare-metal Angara project\n\n"
                 << "[project]\n"
                 << "name = " << name << "\n"
                 << "type = app\n"
                 << "entry = main.an\n"
                 << "freestanding = true\n"
                 << "dependencies = []\n\n"
                 << "[profile]\n"
                 << "mode = release\n"
                 << "opt = 2\n"
                 << "target = " << target << "\n";
        abs_file.close();

        // Create main.an (bare-metal style)
        if (!fs::exists("main.an")) {
            std::ofstream src("main.an");
            src << "// " << name << " — Bare-metal Angara kernel\n"
                << "// Entry point: _start (freestanding mode)\n\n"
                << "// UART MMIO address (example: QEMU virt PL011)\n"
                << "const UART0 = 0x09000000 as *u8;\n\n"
                << "func uart_putc(c: u8) {\n"
                << "    *UART0 = c;\n"
                << "}\n\n"
                << "func uart_puts(s: string) {\n"
                << "    for c in s {\n"
                << "        uart_putc(c as u8);\n"
                << "    }\n"
                << "}\n\n"
                << "export func main() -> i64 {\n"
                << "    uart_puts(\"Hello from " << name << "!\\n\");\n"
                << "    return 0;\n"
                << "}\n";
            src.close();
        }

        // Create a basic linker script
        if (!fs::exists("linker.ld")) {
            std::ofstream ld("linker.ld");
            ld << "ENTRY(_start)\n\n"
               << "SECTIONS {\n"
               << "    . = 0x40080000;\n\n"
               << "    .text : {\n"
               << "        *(.text)\n"
               << "        *(.text.*)\n"
               << "    }\n\n"
               << "    .rodata : {\n"
               << "        *(.rodata)\n"
               << "        *(.rodata.*)\n"
               << "    }\n\n"
               << "    .data : {\n"
               << "        *(.data)\n"
               << "        *(.data.*)\n"
               << "    }\n\n"
               << "    .bss : {\n"
               << "        __bss_start = .;\n"
               << "        *(.bss)\n"
               << "        *(.bss.*)\n"
               << "        *(COMMON)\n"
               << "        __bss_end = .;\n"
               << "    }\n"
               << "}\n";
            ld.close();
        }

        std::cout << CLR_BOLD << CLR_GREEN << "✓ Embedded project created." << CLR_RESET << "\n";
        std::cout << CLR_CYAN << "  Build with: angc" << CLR_RESET << "\n";
        std::cout << CLR_GRAY << "  Link with your bare-metal toolchain using linker.ld" << CLR_RESET << "\n";
        return true;
    }

    // =====================================================
    // Template: GUI (ImGui + OpenGL)
    // =====================================================

    bool ProjectInitializer::init_gui(const std::string& name, const std::string& author) {
        std::cout << CLR_BOLD << CLR_MAGENTA << "--- Creating GUI Project: " << name << " ---" << CLR_RESET << "\n";

        std::ofstream abs_file("project.abs");
        abs_file << "[workspace]\n"
                 << "name = " << name << "\n"
                 << "author = " << author << "\n"
                 << "version = 0.1.0\n"
                 << "description = Angara GUI application with Dear ImGui\n\n"
                 << "[project]\n"
                 << "name = " << name << "\n"
                 << "type = app\n"
                 << "entry = main.an\n"
                 << "dependencies = [imgui]\n\n"
                 << "[native-module]\n"
                 << "name = imgui\n"
                 << "sources = [imgui_glue.cpp]\n"
                 << "include_dirs = [vendor/imgui, vendor/imgui/backends]\n"
                 << "libs = [glfw]\n"
                 << "frameworks = [OpenGL, Cocoa, IOKit, CoreVideo]\n"
                 << "cpp = true\n\n"
                 << "[profile]\n"
                 << "mode = debug\n"
                 << "opt = 0\n";
        abs_file.close();

        // Create main.an
        if (!fs::exists("main.an")) {
            std::ofstream src("main.an");
            src << "attach imgui;\n\n"
                << "// " << name << " — Angara GUI Application\n\n"
                << "export func main() -> i64 {\n"
                << "    // Initialize your GUI here\n"
                << "    io.println(1, \"GUI app started: " << name << "\");\n\n"
                << "    return 0;\n"
                << "}\n";
            src.close();
        }

        std::cout << CLR_BOLD << CLR_GREEN << "✓ GUI project created." << CLR_RESET << "\n";
        std::cout << CLR_YELLOW << "  Note: You'll need Dear ImGui in vendor/imgui/" << CLR_RESET << "\n";
        std::cout << CLR_GRAY << "  Run with: angc run" << CLR_RESET << "\n";
        return true;
    }

    // =====================================================
    // Interactive Mode (original behavior, enhanced)
    // =====================================================

    bool ProjectInitializer::init_interactive() {
        std::cout << CLR_BOLD << CLR_MAGENTA << "--- Angara Project Initialization ---" << CLR_RESET << "\n";
        std::cout << "This will create a new workspace configuration (project.abs).\n\n";

        // 1. Workspace Configuration
        std::string ws_name = prompt("Workspace Name", fs::current_path().filename().string());
        std::string ws_author = prompt("Author Name", "user");
        std::string ws_version = prompt("Version", "0.1.0");
        std::string ws_desc = prompt("Description", "");

        std::stringstream abs_content;
        abs_content << "[workspace]\n";
        abs_content << "name = " << ws_name << "\n";
        abs_content << "author = " << ws_author << "\n";
        abs_content << "version = " << ws_version << "\n";
        if (!ws_desc.empty()) abs_content << "description = " << ws_desc << "\n";
        abs_content << "\n";

        // 2. Project Loop
        bool adding = true;
        while (adding) {
            std::cout << "\n" << CLR_BOLD << CLR_YELLOW << ">> New Project Entry" << CLR_RESET << "\n";

            std::string p_name = prompt("  Project Name");
            if (p_name.empty()) {
                std::cout << "  Name cannot be empty. Skipping...\n";
                continue;
            }

            std::string p_type = prompt("  Type (app/library)", "app");
            std::string p_entry = prompt("  Entry File", "main.an");
            std::string p_deps = prompt("  Dependencies (comma separated, e.g: io, json)", "");
            std::string p_desc = prompt("  Description", "");

            abs_content << "[project]\n";
            abs_content << "name = " << p_name << "\n";
            abs_content << "type = " << p_type << "\n";
            abs_content << "entry = " << p_entry << "\n";
            if (!p_desc.empty()) abs_content << "description = " << p_desc << "\n";

            // Format dependencies list
            if (p_deps.empty()) {
                abs_content << "dependencies = []\n\n";
            } else {
                abs_content << "dependencies = [" << p_deps << "]\n\n";
            }

            // Ask for native module
            if (confirm("  Add a native C/C++ module?")) {
                std::string nm_name = prompt("    Module name", p_name + "_native");
                std::string nm_sources = prompt("    Sources (comma separated)", "glue.c");
                std::string nm_libs = prompt("    Link libraries (comma separated)", "");
                std::string nm_cpp = prompt("    C++? (y/n)", "n");

                abs_content << "[native-module]\n";
                abs_content << "name = " << nm_name << "\n";
                abs_content << "sources = [" << nm_sources << "]\n";
                if (!nm_libs.empty()) abs_content << "libs = [" << nm_libs << "]\n";
                if (nm_cpp == "y" || nm_cpp == "yes") abs_content << "cpp = true\n";
                abs_content << "\n";
            }

            // Ask for pre-build step
            if (confirm("  Add pre-build step?")) {
                std::string pre_cmd = prompt("    Command");
                abs_content << "[pre-build]\n";
                abs_content << "command = " << pre_cmd << "\n\n";
            }

            // Ask for post-build step
            if (confirm("  Add post-build step?")) {
                std::string post_cmd = prompt("    Command");
                abs_content << "[post-build]\n";
                abs_content << "command = " << post_cmd << "\n\n";
            }

            // Build profile
            std::string p_mode = prompt("  Build mode (debug/release)", "debug");
            abs_content << "[profile]\n";
            abs_content << "mode = " << p_mode << "\n\n";

            // Create directories and boilerplate
            try {
                fs::create_directories(p_name);
                std::string full_entry_path = p_name + "/" + p_entry;
                if (!fs::exists(full_entry_path)) {
                    std::ofstream entry_file(full_entry_path);
                    entry_file << "attach io;\n\nexport func main() -> i64 {\n    io.println(1, \"Hello from " << p_name << "!\");\n    return 0;\n}\n";
                }
            } catch (const std::exception& e) {
                std::cerr << "Warning: Could not create folders for " << p_name << ": " << e.what() << "\n";
            }

            adding = confirm("Add another project?");
        }

        // 3. Write .abs file
        std::ofstream outfile("project.abs");
        outfile << abs_content.str();
        outfile.close();

        std::cout << "\n" << CLR_BOLD << CLR_GREEN << "✓ Successfully initialized workspace in project.abs" << CLR_RESET << "\n";
        std::cout << "Run " << CLR_BOLD << "angc" << CLR_RESET << " to build your new projects.\n";

        return true;
    }
}