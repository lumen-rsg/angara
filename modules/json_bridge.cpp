//
// Created by cv2 on 9/29/25.
//

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <iostream>

// This is a C++ file, but it exposes a C-compatible interface.
extern "C" {
#include "json_bridge.h"
}

using json = nlohmann::json;

// --- Implementation of the C Bridge ---

void json_bridge_free_string(char* str) {
    if (str) {
        free(str);
    }
}


JsonHandle json_bridge_parse(const char* json_string, char** error_message) {
    // 1. ALWAYS initialize the output parameter to a known state.
    *error_message = nullptr;

    // 2. Add a guard against null input.
    if (json_string == nullptr) {
        *error_message = strdup("Error: Cannot parse a null JSON string.");
        return nullptr;
    }

    try {
        // The success path is unchanged.
        json* j = new json(json::parse(json_string));
        return static_cast<JsonHandle>(j);
    }
    catch (const json::parse_error& e) {
        // --- THIS IS THE DEFINITIVE FIX ---
        // A. Capture the exception message into a stable C++ std::string.
        std::string what_str = e.what();

        // B. Create a more detailed error message. nlohmann::json provides
        //    the byte position of the error, which is incredibly useful.
        std::stringstream ss;
        ss << "JSON Parse Error: " << what_str;

        // C. Duplicate the final, detailed message onto the heap for the C caller.
        *error_message = strdup(ss.str().c_str());
        return nullptr;
        // --- END FIX ---
    }
    catch (const std::exception& e) {
        // Catch any other standard C++ exceptions (e.g., std::bad_alloc).
        std::string what_str = e.what();
        *error_message = strdup(("Caught C++ exception: " + what_str).c_str());
        return nullptr;
    }
    catch (...) {
        // Catch any non-standard, unknown exceptions.
        *error_message = strdup("An unknown, non-standard C++ exception occurred during JSON parsing.");
        return nullptr;
    }
}

void json_bridge_free(JsonHandle handle) {
    // Cast the opaque pointer back to a C++ object and delete it.
    if (handle) {
        delete static_cast<json*>(handle);
    }
}

// Type checking functions
int json_bridge_is_object(JsonHandle h) { return static_cast<json*>(h)->is_object(); }
int json_bridge_is_array(JsonHandle h) { return static_cast<json*>(h)->is_array(); }
int json_bridge_is_string(JsonHandle h) { return static_cast<json*>(h)->is_string(); }
int json_bridge_is_number(JsonHandle h) { return static_cast<json*>(h)->is_number(); }
int json_bridge_is_boolean(JsonHandle h) { return static_cast<json*>(h)->is_boolean(); }
int json_bridge_is_null(JsonHandle h) { return static_cast<json*>(h)->is_null(); }

// Value getter functions
// Note: These are simplified and not fully safe. A real implementation
// would need to handle memory management for the returned string.
const char* json_bridge_get_string(JsonHandle h) {
    // 1. Get the value into a stable C++ string.
    std::string s = static_cast<json*>(h)->get<std::string>();
    // 2. Return a heap-allocated DUPLICATE of the string.
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
        // Return a pointer to the sub-object.
        // NOTE: The memory is owned by the parent `h`. The caller should not free this handle.
        return &((*j)[index]);
    }
    return nullptr;
}

// --- NEW: Object Iteration Implementation ---
size_t json_bridge_object_size(JsonHandle h) {
    return static_cast<json*>(h)->size();
}

// Helper to get an iterator to the Nth element in a map-like object
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
        // Also return a heap-allocated duplicate of the key.
        return strdup(it.key().c_str());
    }
    return nullptr;
}

JsonHandle json_bridge_object_get_value_at(JsonHandle h, size_t index) {
    json* j = static_cast<json*>(h);
    auto it = get_object_iterator_at(h, index);
    if (it != j->cend()) {
        // The const_cast is necessary because our handle system doesn't
        // distinguish between const and non-const. It's safe as long as
        // the C code treats the returned handle as read-only.
        return const_cast<json*>(&(*it));
    }
    return nullptr;
}