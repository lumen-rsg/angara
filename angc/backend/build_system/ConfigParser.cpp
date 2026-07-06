#include "../../includes/ConfigParser.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>

namespace angara {

    std::string trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (std::string::npos == first) return str;
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, (last - first) + 1);
    }

    std::vector<std::string> parse_list(const std::string& value) {
        std::vector<std::string> list;
        std::string v = trim(value);
        if (v.size() >= 2 && v.front() == '[' && v.back() == ']') {
            v = v.substr(1, v.size() - 2);
            std::stringstream ss(v);
            std::string item;
            while (std::getline(ss, item, ',')) {
                std::string trimmed = trim(item);
                if (!trimmed.empty()) list.push_back(trimmed);
            }
        }
        return list;
    }

    bool has_cpp_sources(const std::vector<std::string>& sources) {
        for (const auto& s : sources) {
            if (s.size() >= 4 && s.substr(s.size() - 4) == ".cpp") return true;
            if (s.size() >= 3 && s.substr(s.size() - 3) == ".cc") return true;
            if (s.size() >= 3 && s.substr(s.size() - 3) == ".cxx") return true;
        }
        return false;
    }

    std::optional<WorkspaceConfig> ConfigParser::parse(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            std::cerr << "Error: Could not open project file '" << path << "'\n";
            return std::nullopt;
        }

        WorkspaceConfig workspace;
        ProjectConfig currentProject;
        NativeModuleConfig currentNativeModule;
        BuildStep currentBuildStep;

        std::string currentSection;

        auto push_project = [&]() {
            if (!currentProject.name.empty()) {
                workspace.projects.push_back(currentProject);
            }
            currentProject = ProjectConfig();
        };

        auto push_native_module = [&]() {
            if (!currentNativeModule.name.empty() && !currentNativeModule.sources.empty()) {
                if (!currentNativeModule.is_cpp) {
                    currentNativeModule.is_cpp = has_cpp_sources(currentNativeModule.sources);
                }
                currentProject.native_modules.push_back(currentNativeModule);
            }
            currentNativeModule = NativeModuleConfig();
        };

        std::string line;
        while (std::getline(file, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;

            if (line.front() == '[' && line.back() == ']') {
                std::string header = line.substr(1, line.size() - 2);

                if (currentSection == "native-module") {
                    push_native_module();
                }

                if (header == "workspace") {
                    currentSection = "workspace";
                } else if (header == "project" || header == "project-entry") {
                    push_project();
                    currentSection = "project";
                } else if (header == "native-module") {
                    currentSection = "native-module";
                } else if (header == "pre-build") {
                    currentSection = "pre-build";
                    currentBuildStep = BuildStep();
                } else if (header == "post-build") {
                    currentSection = "post-build";
                    currentBuildStep = BuildStep();
                } else if (header == "profile") {
                    currentSection = "profile";
                }
                continue;
            }

            size_t eqPos = line.find('=');
            if (eqPos == std::string::npos) continue;

            std::string key = trim(line.substr(0, eqPos));
            std::string value = trim(line.substr(eqPos + 1));

            if (currentSection == "workspace") {
                if (key == "name") workspace.name = value;
                else if (key == "author") workspace.author = value;
                else if (key == "version") workspace.version = value;
                else if (key == "description") workspace.description = value;
                else if (key == "angara_version" || key == "angara-version") workspace.angara_version = value;
            }

            else if (currentSection == "project") {
                if (key == "name") {
                    currentProject.name = value;
                    if (currentProject.path.empty()) currentProject.path = value;
                }
                else if (key == "path") currentProject.path = value;
                else if (key == "author") currentProject.author = value;
                else if (key == "version") currentProject.version = value;
                else if (key == "description") currentProject.description = value;
                else if (key == "entry") currentProject.entry_point = value;
                else if (key == "type") {
                    if (value == "library" || value == "lib") currentProject.type = ProjectType::LIBRARY;
                    else currentProject.type = ProjectType::APP;
                }
                else if (key == "dependencies") {
                    auto raw_list = parse_list(value);
                    for (const auto& item : raw_list) {
                        DependencySpec spec;
                        size_t at_pos = item.find('@');
                        if (at_pos != std::string::npos) {
                            spec.name = item.substr(0, at_pos);
                            spec.version_constraint = item.substr(at_pos + 1);
                        } else {
                            spec.name = item;
                            spec.version_constraint = "*";
                        }
                        currentProject.dependencies.push_back(spec);
                    }
                }
                else if (key == "freestanding") {
                    currentProject.freestanding = (value == "true" || value == "1" || value == "yes");
                }
                else if (key == "nostdlib") {
                    currentProject.nostdlib = (value == "true" || value == "1" || value == "yes");
                }
            }

            else if (currentSection == "native-module") {
                if (key == "name") currentNativeModule.name = value;
                else if (key == "sources") currentNativeModule.sources = parse_list(value);
                else if (key == "include_dirs" || key == "includes") currentNativeModule.include_dirs = parse_list(value);
                else if (key == "libs" || key == "link_libs") currentNativeModule.link_libs = parse_list(value);
                else if (key == "frameworks") currentNativeModule.link_frameworks = parse_list(value);
                else if (key == "cflags") currentNativeModule.cflags = value;
                else if (key == "ldflags") currentNativeModule.ldflags = value;
                else if (key == "cpp" || key == "is_cpp") {
                    currentNativeModule.is_cpp = (value == "true" || value == "1" || value == "yes");
                }
            }

            else if (currentSection == "pre-build") {
                if (key == "command") currentBuildStep.command = value;
                else if (key == "description") currentBuildStep.description = value;
                if (currentProject.pre_build.command.empty() && !value.empty()) {
                    currentProject.pre_build = currentBuildStep;
                }
            }

            else if (currentSection == "post-build") {
                if (key == "command") currentBuildStep.command = value;
                else if (key == "description") currentBuildStep.description = value;
                if (currentProject.post_build.command.empty() && !value.empty()) {
                    currentProject.post_build = currentBuildStep;
                }
            }

            else if (currentSection == "profile") {
                if (key == "mode") {
                    if (value == "release" || value == "Release") currentProject.profile.mode = BuildMode::RELEASE;
                    else currentProject.profile.mode = BuildMode::DEBUG;
                }
                else if (key == "cflags") currentProject.profile.cflags = value;
                else if (key == "ldflags") currentProject.profile.ldflags = value;
                else if (key == "output_dir" || key == "output") currentProject.profile.output_dir = value;
                else if (key == "target") currentProject.profile.target = value;
                else if (key == "opt" || key == "opt_level" || key == "optimization") {
                    try { currentProject.profile.opt_level = std::stoi(value); } catch (const std::exception&) { currentProject.profile.opt_level = 0; }
                }
            }
        }

        if (currentSection == "native-module") {
            push_native_module();
        }
        push_project();

        return workspace;
    }
}
