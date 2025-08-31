#pragma once

#include <string>
#include "Type.h"

namespace angara {
    // Function declarations for pretty-printing any AngaraObject.
    void printObject(const AngaraObject& obj);
    std::string toString(const AngaraObject& obj);
}