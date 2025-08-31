#include "AngaraTrait.h"
#include "AngaraClosure.h"

namespace angara {

    AngaraTrait::AngaraTrait(std::string name, std::map<std::string, std::shared_ptr<AngaraClosure>> methods)
            : name(std::move(name)), methods(std::move(methods)) {}

} // namespace angara