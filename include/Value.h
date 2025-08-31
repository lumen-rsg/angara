#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <memory>
#include <vector>
#include <map>

namespace angara {

    class AngaraString;
    class AngaraList;
    class AngaraRecord;
    class AngaraClosure;
    class AngaraClass;
    class AngaraInstance;

    // --- The New Runtime Value Representation ---
    using AngaraObject = std::variant<
            std::monostate, // Represents 'nil'
            bool,
            int64_t,        // Canonical runtime type for i8, i16, i32, i64, u8, ...
            double,         // Canonical runtime type for f32, f64
            std::shared_ptr<AngaraString>,
            std::shared_ptr<AngaraList>,
            std::shared_ptr<AngaraRecord>,
            std::shared_ptr<AngaraClosure>,
            std::shared_ptr<AngaraClass>,
            std::shared_ptr<AngaraInstance>
    >;

    std::string toString(const AngaraObject& obj);
    void printObject(const AngaraObject& obj);

    struct AngaraString {
        std::string value;
    };

    struct AngaraList {
        std::vector<AngaraObject> elements;
    };

    struct AngaraRecord {
        // Keys must be strings, values can be anything.
        std::map<std::string, AngaraObject> fields;
    };

} // namespace angara
