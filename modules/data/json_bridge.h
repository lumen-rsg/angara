/// JSON bridge — C/AngaraObject <-> nlohmann::json conversion layer. Used by json, rpc, and jwt modules.
#ifndef JSON_BRIDGE_H
#define JSON_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void* JsonHandle;

JsonHandle json_bridge_parse(const char* json_string, char** error_message);

void json_bridge_free(JsonHandle handle);

int json_bridge_is_object(JsonHandle handle);
int json_bridge_is_array(JsonHandle handle);
int json_bridge_is_string(JsonHandle handle);
int json_bridge_is_number(JsonHandle handle);
int json_bridge_is_boolean(JsonHandle handle);
int json_bridge_is_null(JsonHandle handle);
void json_bridge_free_string(char* str);

const char* json_bridge_get_string(JsonHandle handle);
double json_bridge_get_number(JsonHandle handle);
int json_bridge_get_boolean(JsonHandle handle);

size_t json_bridge_array_size(JsonHandle handle);
JsonHandle json_bridge_array_get_element(JsonHandle handle, size_t index);

size_t json_bridge_object_size(JsonHandle handle);
const char* json_bridge_object_get_key_at(JsonHandle handle, size_t index);
JsonHandle json_bridge_object_get_value_at(JsonHandle handle, size_t index);


const char* json_bridge_stringify(JsonHandle handle);
void json_bridge_free_string(char* str);

JsonHandle json_bridge_new_null();
JsonHandle json_bridge_new_bool(int value);
JsonHandle json_bridge_new_number(double value);
JsonHandle json_bridge_new_string(const char* value);
JsonHandle json_bridge_new_object();
JsonHandle json_bridge_new_array();

void json_bridge_object_add(JsonHandle object, const char* key, JsonHandle value);
void json_bridge_array_add(JsonHandle array, JsonHandle value);

#ifdef __cplusplus
}
#endif

#endif