/// Angara JSON module — parse and stringify via nlohmann::json C++ bridge.
#include <stdio.h>
#include <stdlib.h>
#include "Angara.h"
#include "json_bridge.h"

#define IS_STR(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)
#define IS_REC(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_RECORD)
#define IS_LIST(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_LIST)

static AngaraObject convert_json_handle_to_angara(JsonHandle handle);

static AngaraObject convert_cjson_object(JsonHandle object_handle) {
    AngaraObject record = ang_api->record_new();
    size_t size = json_bridge_object_size(object_handle);
    for (size_t i = 0; i < size; ++i) {
        const char* key = json_bridge_object_get_key_at(object_handle, i);
        JsonHandle value_handle = json_bridge_object_get_value_at(object_handle, i);
        if (key && value_handle) {
            AngaraObject value = convert_json_handle_to_angara(value_handle);
            ang_api->record_set(record, key, value);
            ang_api->decref(value);
        }
        if (key) free((void*)key);
    }
    return record;
}

static AngaraObject convert_cjson_array(JsonHandle array_handle) {
    AngaraObject list = ang_api->list_new();
    size_t size = json_bridge_array_size(array_handle);
    for (size_t i = 0; i < size; ++i) {
        JsonHandle element_handle = json_bridge_array_get_element(array_handle, i);
        if (element_handle) {
            AngaraObject value = convert_json_handle_to_angara(element_handle);
            ang_api->list_push(list, value);
            ang_api->decref(value);
        }
    }
    return list;
}

static AngaraObject convert_json_handle_to_angara(JsonHandle handle) {
    if (json_bridge_is_object(handle))  return convert_cjson_object(handle);
    if (json_bridge_is_array(handle))   return convert_cjson_array(handle);
    if (json_bridge_is_string(handle)) {
        const char* c_str = json_bridge_get_string(handle);
        AngaraObject s = ang_api->string(c_str);
        if (c_str) free((void*)c_str);
        return s;
    }
    if (json_bridge_is_number(handle))  return ang_f64(json_bridge_get_number(handle));
    if (json_bridge_is_boolean(handle)) return ang_bool(json_bridge_get_boolean(handle));
    if (json_bridge_is_null(handle))    return ang_nil();
    return ang_nil();
}

AngaraObject Angara_json_parse(int arg_count, AngaraObject args[]) {

    char* error_msg = NULL;
    JsonHandle handle = json_bridge_parse(ang_api->as_cstr(args[0]), &error_msg);

    if (handle == NULL) {
        if (error_msg) {
            ang_api->throw_error(error_msg);
            free(error_msg);
        } else {
            ang_api->throw_error("Unknown JSON parse error.");
        }
        return ang_nil();
    }

    AngaraObject result = convert_json_handle_to_angara(handle);
    json_bridge_free(handle);
    return result;
}

static JsonHandle convert_angara_to_json_handle(AngaraObject obj) {
    if (ang_is_nil(obj))   return json_bridge_new_null();
    if (ang_is_bool(obj))  return json_bridge_new_bool(ang_as_bool(obj));
    if (ang_is_i64(obj))   return json_bridge_new_number((double)ang_as_i64(obj));
    if (ang_is_f64(obj))   return json_bridge_new_number(ang_as_f64(obj));

    if (ang_is_obj(obj)) {
        int32_t otype = ang_api->obj_type(obj);
        if (otype == ANG_OBJ_STRING) {
            return json_bridge_new_string(ang_api->as_cstr(obj));
        }
        if (otype == ANG_OBJ_LIST) {
            JsonHandle h = json_bridge_new_array();
            size_t len = ang_api->list_len(obj);
            for (size_t i = 0; i < len; ++i) {
                AngaraObject elem = ang_api->list_get(obj, (int64_t)i);
                JsonHandle val_h = convert_angara_to_json_handle(elem);
                json_bridge_array_add(h, val_h);
                json_bridge_free(val_h);
                ang_api->decref(elem);
            }
            return h;
        }
        if (otype == ANG_OBJ_RECORD) {
            JsonHandle h = json_bridge_new_object();
            size_t len = ang_api->record_len(obj);
            for (size_t i = 0; i < len; ++i) {
                const char* key = ang_api->record_key_at(obj, i);
                AngaraObject val = ang_api->record_val_at(obj, i);
                JsonHandle val_h = convert_angara_to_json_handle(val);
                json_bridge_object_add(h, key, val_h);
                json_bridge_free(val_h);
                ang_api->decref(val);
            }
            return h;
        }
    }
    return json_bridge_new_null();
}

AngaraObject Angara_json_stringify(int arg_count, AngaraObject args[]) {

    JsonHandle handle = convert_angara_to_json_handle(args[0]);
    if (!handle) return ang_api->string("null");

    const char* c_str = json_bridge_stringify(handle);
    AngaraObject result = ang_api->string(c_str);
    json_bridge_free_string((char*)c_str);
    json_bridge_free(handle);
    return result;
}

static const AngaraFuncDef JSON_EXPORTS[] = {
    {"parse",     Angara_json_parse,     "s->a", NULL},
    {"stringify", Angara_json_stringify, "a->s", NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(json) {
    ang_api = api;
    *def_count = (sizeof(JSON_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return JSON_EXPORTS;
}