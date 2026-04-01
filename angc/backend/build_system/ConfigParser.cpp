#include "../../includes/ConfigParser.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>

namespace angara {

    // Helper to trim whitespace
    std::string trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (std::string::npos == first) return str;
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, (last - first) + 1);
    }

    // Helper to parse lists: "[a, b, c]" -> vector
    std::vector<std::string> parse_list(std::string value) {
        std::vector<std::string> list;
        if (value.front() == '[' && value.back() == ']') {
            value = value.substr(1, value.size() - 2); // Strip []
            std::stringstream ss(value);
            std::string item;
            while (std::getline(ss, item, ',')) {
                list.push_back(trim(item));
            }
        }
        return list;
    }

    std::optional<WorkspaceConfig> ConfigParser::parse(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            std::cerr << "Error: Could not open project file '" << path << "'\n";
            return std::nullopt;
        }

        WorkspaceConfig workspace;
        ProjectConfig currentProject;

        std::string line;
        std::string currentSection = "";
        bool inProjectsBlock = false;

        while (std::getline(file, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;

            // Handle Tags
            if (line == "<projects>") {
                inProjectsBlock = true;
                continue;
            }
            if (line == "<!projects>") {
                inProjectsBlock = false;
                continue;
            }

            // Handle Headers
            if (line.front() == '[' && line.back() == ']') {
                std::string header = line.substr(1, line.size() - 2);

                if (header == "workspace") {
                    currentSection = "workspace";
                } else if (header == "project" || header == "project-entry") {
                    if (!currentProject.name.empty()) {
                        // Push previous project before starting new one
                        workspace.projects.push_back(currentProject);
                    }
                    currentProject = ProjectConfig(); // Reset
                    currentSection = "project";
                }
                continue;
            }

            // Handle Key-Value pairs
            size_t eqPos = line.find('=');
            if (eqPos != std::string::npos) {
                std::string key = trim(line.substr(0, eqPos));
                std::string value = trim(line.substr(eqPos + 1));

                if (currentSection == "workspace") {
                    if (key == "name") workspace.name = value;
                    else if (key == "author") workspace.author = value;
                    else if (key == "version") workspace.version = value;
                }
                else if (currentSection == "project") {
                    if (key == "name") {
                        currentProject.name = value;
                        if (currentProject.path.empty()) currentProject.path = value; // Default path to name
                    }
                    else if (key == "path") currentProject.path = value;
                    else if (key == "author") currentProject.author = value;
                    else if (key == "version") currentProject.version = value;
                    else if (key == "entry") currentProject.entry_point = value;
                    else if (key == "type") {
                        if (value == "library" || value == "lib") currentProject.type = ProjectType::LIBRARY;
                        else currentProject.type = ProjectType::APP;
                    }
                    else if (key == "dependencies") {
                        currentProject.dependencies = parse_list(value);
                    }
                }
            }
        }

        // Push the final project
        if (!currentProject.name.empty()) {
            workspace.projects.push_back(currentProject);
        }

        return workspace;
    }
}