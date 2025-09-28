//
// Created by cv2 on 9/29/25.
//

#ifndef JSON_BRIDGE_H
#define JSON_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

// A handle to an opaque C++ json object.
typedef void* JsonHandle;

// Parses a string and returns a handle, or NULL on error.
// The error string must be freed by the caller if not NULL.
JsonHandle json_bridge_parse(const char* json_string, char** error_message);

// Frees the memory for a JSON handle.
void json_bridge_free(JsonHandle handle);

// Functions to inspect the type of a JSON node.
int json_bridge_is_object(JsonHandle handle);
int json_bridge_is_array(JsonHandle handle);
int json_bridge_is_string(JsonHandle handle);
int json_bridge_is_number(JsonHandle handle);
int json_bridge_is_boolean(JsonHandle handle);
int json_bridge_is_null(JsonHandle handle);
void json_bridge_free_string(char* str);

// Functions to get the value of a JSON node.
const char* json_bridge_get_string(JsonHandle handle);
double json_bridge_get_number(JsonHandle handle);
int json_bridge_get_boolean(JsonHandle handle);

// --- NEW: Array Iteration API ---
// Returns the number of elements in a JSON array.
size_t json_bridge_array_size(JsonHandle handle);
// Returns a handle to the element at a given index in a JSON array.
JsonHandle json_bridge_array_get_element(JsonHandle handle, size_t index);

// --- NEW: Object Iteration API ---
// Returns the number of key-value pairs in a JSON object.
size_t json_bridge_object_size(JsonHandle handle);
// Returns the key at a given index in a JSON object. Caller must free.
const char* json_bridge_object_get_key_at(JsonHandle handle, size_t index);
// Returns a handle to the value at a given index in a JSON object.
JsonHandle json_bridge_object_get_value_at(JsonHandle handle, size_t index);

// Functions for iterating over objects and arrays.
// These are more complex and would be a great next step, but for now
// we can provide a simplified API. A full implementation would return
// iterators or arrays of keys/values.

#ifdef __cplusplus
}
#endif

#endif // JSON_BRIDGE_H