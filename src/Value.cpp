//
// Created by cv2 on 8/31/25.
//
#include "Value.h"
#include "AngaraClass.h"
#include "AngaraClosure.h"
#include "AngaraInstance.h"
#include <sstream>
#include <iostream>

namespace angara {

// Forward-declare to allow mutual recursion (e.g., lists in lists)
    std::string toString(const AngaraObject& obj);

    void printObject(const AngaraObject& obj) {
        std::cout << toString(obj);
    }

    std::string toString(const AngaraObject& obj) {
        std::stringstream ss;
        if (std::holds_alternative<std::monostate>(obj)) {
            ss << "nil";
        } else if (std::holds_alternative<bool>(obj)) {
            ss << (std::get<bool>(obj) ? "true" : "false");
        } else if (std::holds_alternative<int64_t>(obj)) {
            ss << std::get<int64_t>(obj);
        } else if (std::holds_alternative<double>(obj)) {
            ss << std::get<double>(obj);
        } else if (std::holds_alternative<std::shared_ptr<AngaraString>>(obj)) {
            ss << std::get<std::shared_ptr<AngaraString>>(obj)->value;
        } else if (std::holds_alternative<std::shared_ptr<AngaraList>>(obj)) {
            auto list = std::get<std::shared_ptr<AngaraList>>(obj);
            ss << "[";
            for (size_t i = 0; i < list->elements.size(); ++i) {
                ss << toString(list->elements[i]);
                if (i < list->elements.size() - 1) ss << ", ";
            }
            ss << "]";
        } else if (std::holds_alternative<std::shared_ptr<AngaraRecord>>(obj)) {
            auto record = std::get<std::shared_ptr<AngaraRecord>>(obj);
            ss << "{";
            for (auto it = record->fields.begin(); it != record->fields.end(); ++it) {
                ss << "\"" << it->first << "\": " << toString(it->second);
                if (std::next(it) != record->fields.end()) ss << ", ";
            }
            ss << "}";
        } else if (std::holds_alternative<std::shared_ptr<AngaraClosure>>(obj)) {
            auto closure = std::get<std::shared_ptr<AngaraClosure>>(obj);
            if (closure->isNative()) {
                ss << "<native fn>";
            } else {
                ss << "<fn " << closure->getScriptFunction()->name << ">";
            }
        } else if (std::holds_alternative<std::shared_ptr<AngaraClass>>(obj)) {
            ss << "<class " << std::get<std::shared_ptr<AngaraClass>>(obj)->name << ">";
        } else if (std::holds_alternative<std::shared_ptr<AngaraInstance>>(obj)) {
            ss << "<instance of " << std::get<std::shared_ptr<AngaraInstance>>(obj)->m_class->name << ">";
        } else {
            ss << "<unknown object>";
        }
        return ss.str();
    }

} // namespace angara