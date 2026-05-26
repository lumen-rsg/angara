/// JSON bridge — C/AngaraObject <-> nlohmann::json conversion layer. Used by json, rpc, and jwt modules.
#include "json.hpp"
#include <string>
#include <vector>
#include <iostream>
#include <sstream>

extern "C" {
#include "json_bridge.h"
}

using json = nlohmann::json;

void json_bridge_free_string(char* str) {
    if (str) {
        free(str);
    }
}


JsonHandle json_bridge_parse(const char* json_string, char** error_message) {
    *error_message = nullptr;

    if (json_string == nullptr) {
        *error_message = strdup("Error: Cannot parse a null JSON string.");
        return nullptr;
    }

    try {
        json* j = new json(json::parse(json_string));
        return static_cast<JsonHandle>(j);
    }
    catch (const json::parse_error& e) {
        std::string what_str = e.what();

        std::stringstream ss;
        ss << "JSON Parse Error: " << what_str;

        *error_message = strdup(ss.str().c_str());
        return nullptr;
    }
    catch (const std::exception& e) {
        std::string what_str = e.what();
        *error_message = strdup(("Caught C++ exception: " + what_str).c_str());
        return nullptr;
    }
    catch (...) {
        *error_message = strdup("An unknown, non-standard C++ exception occurred during JSON parsing.");
        return nullptr;
    }
}

void json_bridge_free(JsonHandle handle) {
    if (handle) {
        delete static_cast<json*>(handle);
    }
}

int json_bridge_is_object(JsonHandle h) { return static_cast<json*>(h)->is_object(); }
int json_bridge_is_array(JsonHandle h) { return static_cast<json*>(h)->is_array(); }
int json_bridge_is_string(JsonHandle h) { return static_cast<json*>(h)->is_string(); }
int json_bridge_is_number(JsonHandle h) { return static_cast<json*>(h)->is_number(); }
int json_bridge_is_boolean(JsonHandle h) { return static_cast<json*>(h)->is_boolean(); }
int json_bridge_is_null(JsonHandle h) { return static_cast<json*>(h)->is_null(); }

const char* json_bridge_get_string(JsonHandle h) {
    std::string s = static_cast<json*>(h)->get<std::string>();
    return strdup(s.c_str());
}
double json_bridge_get_number(JsonHandle h) { return static_cast<json*>(h)->get<double>(); }
int json_bridge_get_boolean(JsonHandle h) { return static_cast<json*>(h)->get<bool>(); }

size_t json_bridge_array_size(JsonHandle h) {
    return static_cast<json*>(h)->size();
}
JsonHandle json_bridge_array_get_element(JsonHandle h, size_t index) {
    json* j = static_cast<json*>(h);
    if (index < j->size()) {
        return &((*j)[index]);
    }
    return nullptr;
}

size_t json_bridge_object_size(JsonHandle h) {
    return static_cast<json*>(h)->size();
}

json::const_iterator get_object_iterator_at(JsonHandle h, size_t index) {
    json* j = static_cast<json*>(h);
    if (index < j->size()) {
        auto it = j->cbegin();
        std::advance(it, index);
        return it;
    }
    return j->cend();
}

const char* json_bridge_object_get_key_at(JsonHandle h, size_t index) {
    json* j = static_cast<json*>(h);
    auto it = get_object_iterator_at(h, index);
    if (it != j->cend()) {
        return strdup(it.key().c_str());
    }
    return nullptr;
}

JsonHandle json_bridge_object_get_value_at(JsonHandle h, size_t index) {
    json* j = static_cast<json*>(h);
    auto it = get_object_iterator_at(h, index);
    if (it != j->cend()) {
        return const_cast<json*>(&(*it));
    }
    return nullptr;
}

const char* json_bridge_stringify(JsonHandle h) {
    if (!h) return strdup("null");
    std::string s = static_cast<json*>(h)->dump();
    return strdup(s.c_str());
}

JsonHandle json_bridge_new_null()   { return new json(); }
JsonHandle json_bridge_new_bool(int v)   { return new json(v != 0); }
JsonHandle json_bridge_new_number(double v) { return new json(v); }
JsonHandle json_bridge_new_string(const char* v) { return new json(v); }
JsonHandle json_bridge_new_object() { return new json(json::object()); }
JsonHandle json_bridge_new_array()  { return new json(json::array()); }

void json_bridge_object_add(JsonHandle h, const char* key, JsonHandle vh) {
    json* obj = static_cast<json*>(h);
    json* val = static_cast<json*>(vh);
    if (obj && key && val) {
        (*obj)[key] = *val;
    }
}
void json_bridge_array_add(JsonHandle h, JsonHandle vh) {
    json* arr = static_cast<json*>(h);
    json* val = static_cast<json*>(vh);
    if (arr && val) {
        arr->push_back(*val);
    }
}