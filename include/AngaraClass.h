#pragma once

#include <string>
#include <map>
#include <memory>
#include "AngaraClosure.h" // Methods are closures

namespace angara {

    class AngaraClass {
    public:
        explicit AngaraClass(std::string name);

        const std::string name;
        std::map<std::string, std::shared_ptr<AngaraClosure>> methods;
    };

} // namespace angara