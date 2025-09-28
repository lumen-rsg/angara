#include "../runtime/angara_runtime.h"
#include "json_bridge.h"
#include <stdio.h>
#include <stdlib.h>

// --- Forward Declaration ---
static AngaraObject convert_json_handle_to_angara(JsonHandle handle);

// --- Recursive Conversion Logic (Now Complete) ---

static AngaraObject convert_cjson_object(JsonHandle object_handle) {
    AngaraObject record = angara_record_new();
    size_t size = json_bridge_object_size(object_handle);

    for (size_t i = 0; i < size; ++i) {
        // 1. Get the key from the bridge (it's now malloc'd).
        const char* key = json_bridge_object_get_key_at(object_handle, i);
        JsonHandle value_handle = json_bridge_object_get_value_at(object_handle, i);

        if (key && value_handle) {
            AngaraObject value = convert_json_handle_to_angara(value_handle);
            angara_record_set(record, key, value);
            angara_decref(value);
        }

        // 2. We are done with the key, so we MUST free it.
        // Since strdup uses malloc, we can just use free.
        if (key) {
            free((void*)key);
        }
    }
    return record;
}

static AngaraObject convert_cjson_array(JsonHandle array_handle) {
    AngaraObject list = angara_list_new();
    size_t size = json_bridge_array_size(array_handle);

    for (size_t i = 0; i < size; ++i) {
        JsonHandle element_handle = json_bridge_array_get_element(array_handle, i);
        if (element_handle) {
            AngaraObject value = convert_json_handle_to_angara(element_handle);
            angara_list_push(list, value);
            angara_decref(value); // list_push takes ownership
        }
    }
    return list;
}

static AngaraObject convert_json_handle_to_angara(JsonHandle handle) {
    if (json_bridge_is_object(handle))  return convert_cjson_object(handle);
    if (json_bridge_is_array(handle))   return convert_cjson_array(handle);
    if (json_bridge_is_string(handle)) {
        // 1. Get the string from the bridge (it's malloc'd).
        const char* c_str = json_bridge_get_string(handle);
        // 2. angara_create_string makes its OWN copy internally.
        AngaraObject angara_str = angara_create_string(c_str);
        // 3. We are done with our copy, so we MUST free it.
        if (c_str) {
            free((void*)c_str);
        }
        return angara_str;
    }
    if (json_bridge_is_number(handle))  return angara_create_f64(json_bridge_get_number(handle));
    if (json_bridge_is_boolean(handle)) return angara_create_bool(json_bridge_get_boolean(handle));
    if (json_bridge_is_null(handle))    return angara_create_nil();
    return angara_create_nil();
}

// --- Angara-Exported Function: json.parse (Now Fully Functional) ---
AngaraObject Angara_json_parse(int arg_count, AngaraObject args[]) {
    if (arg_count != 1 || !IS_STRING(args[0])) {
        angara_throw_error("json.parse() requires one string argument.");
        return angara_create_nil();
    }

    char* error_msg = NULL;
    JsonHandle handle = json_bridge_parse(AS_CSTRING(args[0]), &error_msg);

    if (handle == NULL) {
        // This block is now guaranteed to work correctly.
        if (error_msg != NULL) {
            // error_msg is a valid, heap-allocated C string.
            // angara_throw_error will wrap it in an AngaraException.
            angara_throw_error(error_msg);

            // We are now the owner of the error_msg memory and MUST free it.
            free(error_msg);
        } else {
            // This is a fallback for the unlikely case that the bridge
            // returned NULL for both the handle and the error message.
            angara_throw_error("Unknown JSON parse error (bridge returned null).");
        }
        return angara_create_nil(); // This line is technically unreachable.
    }

    // This now performs a full, deep conversion.
    AngaraObject result = convert_json_handle_to_angara(handle);

    // Clean up the top-level memory used by the C++ object.
    json_bridge_free(handle);

    return result;
}



// --- ABI Definition Table ---
static const AngaraFuncDef JSON_EXPORTS[] = {
        {
                "parse",
                Angara_json_parse,
                "s->a", // Takes a string, returns `any`
                           NULL
        },
        {NULL, NULL, NULL, NULL}
};

// --- Module Entry Point ---
const AngaraFuncDef* Angara_json_Init(int* def_count) {
    *def_count = (sizeof(JSON_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return JSON_EXPORTS;
}