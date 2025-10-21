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

static JsonHandle convert_angara_to_json_handle(AngaraObject obj);

static JsonHandle convert_angara_record(AngaraObject record_obj) {
    JsonHandle h = json_bridge_new_object();
    AngaraRecord* record = AS_RECORD(record_obj);
    for (size_t i = 0; i < record->count; ++i) {
        const char* key = record->entries[i].key;
        AngaraObject val_obj = record->entries[i].value;

        JsonHandle val_h = convert_angara_to_json_handle(val_obj);
        json_bridge_object_add(h, key, val_h);

        // The object_add function copies the value, so we must free the
        // temporary handle we created for the value.
        json_bridge_free(val_h);
    }
    return h;
}

static JsonHandle convert_angara_list(AngaraObject list_obj) {
    JsonHandle h = json_bridge_new_array();
    AngaraList* list = AS_LIST(list_obj);
    for (size_t i = 0; i < list->count; ++i) {
        JsonHandle val_h = convert_angara_to_json_handle(list->elements[i]);
        json_bridge_array_add(h, val_h);

        // The array_add function copies the value, so we must free the temporary handle.
        json_bridge_free(val_h);
    }
    return h;
}

static JsonHandle convert_angara_to_json_handle(AngaraObject obj) {
    switch (obj.type) {
        case VAL_NIL:
            return json_bridge_new_null();
        case VAL_BOOL:
            return json_bridge_new_bool(AS_BOOL(obj));
        case VAL_I64:
            // JSON spec technically doesn't have integers, just numbers.
            // We convert to double for compatibility.
            return json_bridge_new_number((double)AS_I64(obj));
        case VAL_F64:
            return json_bridge_new_number(AS_F64(obj));
        case VAL_OBJ:
            switch (OBJ_TYPE(obj)) {
                case OBJ_STRING:
                    return json_bridge_new_string(AS_CSTRING(obj));
                case OBJ_LIST:
                    return convert_angara_list(obj);
                case OBJ_RECORD:
                    return convert_angara_record(obj);
                default:
                    // Cannot serialize other object types (functions, instances, etc.)
                    // We'll represent them as null in the JSON.
                    return json_bridge_new_null();
            }
        default:
            return json_bridge_new_null();
    }
}

// --- Angara-Exported Function: json.stringify ---
AngaraObject Angara_json_stringify(int arg_count, AngaraObject args[]) {
    if (arg_count != 1) {
        angara_throw_error("json.stringify() requires one argument.");
        return angara_create_nil();
    }

    // 1. Recursively convert the Angara object to a C++ JSON handle.
    JsonHandle handle = convert_angara_to_json_handle(args[0]);
    if (handle == NULL) {
        // This should not happen with the new logic, but as a safeguard:
        return angara_create_string("null");
    }

    // 2. Call the bridge to serialize the handle to a C string.
    const char* c_str = json_bridge_stringify(handle);

    // 3. Convert the C string to an Angara string.
    AngaraObject angara_str = angara_create_string(c_str);

    // 4. Free the memory allocated by the bridge.
    json_bridge_free_string((char*)c_str);
    json_bridge_free(handle);

    return angara_str;
}



// --- ABI Definition Table ---
static const AngaraFuncDef JSON_EXPORTS[] = {
    {"parse",     Angara_json_parse,     "s->a", NULL},
    {"stringify", Angara_json_stringify, "a->s", NULL}, // <-- ADD THIS
    {NULL, NULL, NULL, NULL}
};

// --- Module Entry Point ---
const AngaraFuncDef* Angara_json_Init(int* def_count) {
    *def_count = (sizeof(JSON_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return JSON_EXPORTS;
}