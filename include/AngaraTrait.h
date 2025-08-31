#pragma once

#include <string>
#include <map>
#include <memory>

namespace angara {

    class AngaraClosure;

    class AngaraTrait {
    public:
        AngaraTrait(std::string name, std::map<std::string, std::shared_ptr<AngaraClosure>> methods);

        const std::string name;
        const std::map<std::string, std::shared_ptr<AngaraClosure>> methods;
    };

} // namespace angara