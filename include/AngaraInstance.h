#pragma once

#include <map>
#include <string>
#include <memory>
#include "Value.h"

namespace angara {

    class AngaraClass;

    class AngaraInstance {
    public:
        explicit AngaraInstance(std::shared_ptr<AngaraClass> klass);

        std::shared_ptr<AngaraClass> m_class;
        std::map<std::string, AngaraObject> fields;
    };

} // namespace angara