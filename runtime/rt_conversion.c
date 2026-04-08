//
// Angara Runtime — Type conversions, equality, typeof, deep_clone
//

#include "rt_internal.h"

// --- Length ---
AngaraObject angara_len(AngaraObject collection) {
    if (IS_OBJ(collection)) {
        if (OBJ_TYPE(collection) == OBJ_STRING) return angara_create_i64(AS_STRING(collection)->length);
        if (OBJ_TYPE(collection) == OBJ_LIST)   return angara_create_i64(AS_LIST(collection)->count);
    }
    return angara_create_nil();
}

int64_t angara_len_ptr(void* collection) {
    Object* o = (Object*)collection;
    if (o->type == OBJ_STRING) return ((AngaraString*)o)->length;
    if (o->type == OBJ_LIST)   return ((AngaraList*)o)->count;
    return 0;
}

// --- Typeof ---
AngaraObject angara_typeof(AngaraObject value) {
    switch (value.type) {
        case VAL_NIL:   return angara_string_from_c("nil");
        case VAL_BOOL:  return angara_string_from_c("bool");
        case VAL_I64:   return angara_string_from_c("i64");
        case VAL_F64:   return angara_string_from_c("f64");
        case VAL_OBJ:
            switch (OBJ_TYPE(value)) {
                case OBJ_STRING:         return angara_string_from_c("string");
                case OBJ_LIST:           return angara_string_from_c("list");
                case OBJ_RECORD:         return angara_string_from_c("record");
                case OBJ_CLOSURE:        return angara_string_from_c("function");
                case OBJ_CLASS:          return angara_string_from_c("class");
                case OBJ_INSTANCE:       return angara_string_from_c("instance");
                case OBJ_THREAD:         return angara_string_from_c("Thread");
                case OBJ_MUTEX:          return angara_string_from_c("Mutex");
                case OBJ_EXCEPTION:      return angara_string_from_c("Exception");
                case OBJ_DATA_INSTANCE: {
                    AngaraDataInstanceHeader* h = (AngaraDataInstanceHeader*)AS_OBJ(value);
                    return angara_string_from_c(h->info->name);
                }
                case OBJ_ENUM_INSTANCE: {
                    AngaraEnumInstanceHeader* h = (AngaraEnumInstanceHeader*)AS_OBJ(value);
                    return angara_string_from_c(h->info->name);
                }
                case OBJ_BOUND_METHOD: return angara_string_from_c("bound_method");
                default:               return angara_string_from_c("unknown object");
            }
        default:
            return angara_string_from_c("unknown");
    }
}

// --- Conversions ---
AngaraObject angara_to_i64(AngaraObject value) {
    switch (value.type) {
        case VAL_NIL:   return angara_create_i64(0);
        case VAL_BOOL:  return angara_create_i64(AS_BOOL(value) ? 1 : 0);
        case VAL_I64:   angara_incref(value); return value;
        case VAL_F64:   return angara_create_i64((int64_t)AS_F64(value));
        case VAL_OBJ: {
            if (OBJ_TYPE(value) == OBJ_STRING) {
                return angara_create_i64(strtoll(AS_CSTRING(value), NULL, 10));
            }
            return angara_create_i64(0);
        }
        default: return angara_create_i64(0);
    }
}

AngaraObject angara_to_f64(AngaraObject value) {
    switch (value.type) {
        case VAL_NIL:   return angara_create_f64(0.0);
        case VAL_BOOL:  return angara_create_f64(AS_BOOL(value) ? 1.0 : 0.0);
        case VAL_I64:   return angara_create_f64((double)AS_I64(value));
        case VAL_F64:   angara_incref(value); return value;
        case VAL_OBJ: {
            if (OBJ_TYPE(value) == OBJ_STRING) {
                return angara_create_f64(strtod(AS_CSTRING(value), NULL));
            }
            return angara_create_f64(0.0);
        }
        default: return angara_create_f64(0.0);
    }
}

AngaraObject angara_to_bool(AngaraObject value) {
    return angara_create_bool(angara_is_truthy(value));
}

AngaraObject angara_to_string(AngaraObject value) {
    switch (value.type) {
        case VAL_NIL:
            return angara_string_from_c("nil");
        case VAL_BOOL:
            return angara_string_from_c(AS_BOOL(value) ? "true" : "false");
        case VAL_I64: {
            char buffer[22];
            int len = snprintf(buffer, sizeof(buffer), "%lld", AS_I64(value));
            return angara_create_string_no_copy(strdup(buffer), len);
        }
        case VAL_F64: {
            char buffer[32];
            int len = snprintf(buffer, sizeof(buffer), "%g", AS_F64(value));
            return angara_create_string_no_copy(strdup(buffer), len);
        }
        case VAL_OBJ: {
            if (OBJ_TYPE(value) == OBJ_STRING) {
                angara_incref(value);
                return value;
            }
            if (OBJ_TYPE(value) == OBJ_EXCEPTION) {
                AngaraException* exc = AS_EXCEPTION(value);
                angara_incref(exc->message);
                return exc->message;
            }
            if (OBJ_TYPE(value) == OBJ_LIST) {
                AngaraList* list = AS_LIST(value);
                if (list->count == 0) return angara_string_from_c("[]");

                StringBuilder sb;
                sb_init(&sb);
                sb_append(&sb, "[");

                for (size_t i = 0; i < list->count; i++) {
                    AngaraObject elem_str = angara_to_string(list->elements[i]);
                    sb_append(&sb, AS_CSTRING(elem_str));
                    angara_decref(elem_str);
                    if (i < list->count - 1) sb_append(&sb, ", ");
                }
                sb_append(&sb, "]");
                return sb_to_string_obj(&sb);
            }
            if (OBJ_TYPE(value) == OBJ_RECORD) {
                AngaraRecord* record = AS_RECORD(value);
                if (record->count == 0) return angara_string_from_c("{}");

                StringBuilder sb;
                sb_init(&sb);
                sb_append(&sb, "{");

                for (size_t i = 0; i < record->count; i++) {
                    sb_append(&sb, "\"");
                    sb_append(&sb, record->entries[i].key);
                    sb_append(&sb, "\": ");

                    AngaraObject val_str = angara_to_string(record->entries[i].value);
                    sb_append(&sb, AS_CSTRING(val_str));
                    angara_decref(val_str);

                    if (i < record->count - 1) sb_append(&sb, ", ");
                }
                sb_append(&sb, "}");
                return sb_to_string_obj(&sb);
            }
            if (OBJ_TYPE(value) == OBJ_DATA_INSTANCE) {
                AngaraDataInstanceHeader* h = (AngaraDataInstanceHeader*)AS_OBJ(value);
                char buffer[128];
                snprintf(buffer, sizeof(buffer), "<%s object>", h->info->name);
                return angara_string_from_c(buffer);
            }
            if (OBJ_TYPE(value) == OBJ_ENUM_INSTANCE) {
                AngaraEnumInstanceHeader* h = (AngaraEnumInstanceHeader*)AS_OBJ(value);
                char buffer[128];
                snprintf(buffer, sizeof(buffer), "<%s>", h->info->name);
                return angara_string_from_c(buffer);
            }

            AngaraObject type_name_obj = angara_typeof(value);
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "<%s object>", AS_CSTRING(type_name_obj));
            angara_decref(type_name_obj);
            return angara_string_from_c(buffer);
        }
        default:
            return angara_string_from_c("unknown");
    }
}

const char* angara_to_string_raw(AngaraObject val) {
    AngaraObject strObj = angara_to_string(val);
    return AS_CSTRING(strObj);
}

// --- Equality ---
AngaraObject angara_equals(AngaraObject a, AngaraObject b) {
    if (a.type != b.type) {
        if ((IS_I64(a) || IS_F64(a)) && (IS_I64(b) || IS_F64(b))) {
            return angara_create_bool(AS_F64(a) == AS_F64(b));
        }
        return angara_create_bool(false);
    }

    switch (a.type) {
        case VAL_NIL:  return angara_create_bool(true);
        case VAL_BOOL: return angara_create_bool(AS_BOOL(a) == AS_BOOL(b));
        case VAL_I64:  return angara_create_bool(AS_I64(a) == AS_I64(b));
        case VAL_F64:  return angara_create_bool(AS_F64(a) == AS_F64(b));

        case VAL_OBJ: {
            if (AS_OBJ(a) == AS_OBJ(b)) return angara_create_bool(true);
            if (OBJ_TYPE(a) != OBJ_TYPE(b)) return angara_create_bool(false);

            switch (OBJ_TYPE(a)) {
                case OBJ_STRING:
                    return angara_create_bool(strcmp(AS_CSTRING(a), AS_CSTRING(b)) == 0);

                case OBJ_LIST: {
                    AngaraList* l1 = AS_LIST(a);
                    AngaraList* l2 = AS_LIST(b);
                    if (l1->count != l2->count) return angara_create_bool(false);
                    for (size_t i = 0; i < l1->count; i++) {
                        if (!AS_BOOL(angara_equals(l1->elements[i], l2->elements[i]))) {
                            return angara_create_bool(false);
                        }
                    }
                    return angara_create_bool(true);
                }

                case OBJ_RECORD: {
                    AngaraRecord* r1 = AS_RECORD(a);
                    AngaraRecord* r2 = AS_RECORD(b);
                    if (r1->count != r2->count) return angara_create_bool(false);
                    for (size_t i = 0; i < r1->count; i++) {
                        char* key = r1->entries[i].key;
                        AngaraObject val1 = r1->entries[i].value;
                        bool found = false;
                        for (size_t j = 0; j < r2->count; j++) {
                            if (strcmp(r2->entries[j].key, key) == 0) {
                                if (!AS_BOOL(angara_equals(val1, r2->entries[j].value))) {
                                    return angara_create_bool(false);
                                }
                                found = true;
                                break;
                            }
                        }
                        if (!found) return angara_create_bool(false);
                    }
                    return angara_create_bool(true);
                }

                case OBJ_DATA_INSTANCE: {
                    AngaraDataInstanceHeader* d1 = (AngaraDataInstanceHeader*)AS_OBJ(a);
                    AngaraDataInstanceHeader* d2 = (AngaraDataInstanceHeader*)AS_OBJ(b);
                    if (d1->info != d2->info) return angara_create_bool(false);
                    if (d1->info->equals_fn) {
                        return angara_create_bool(d1->info->equals_fn(d1, d2));
                    }
                    return angara_create_bool(d1 == d2);
                }

                case OBJ_ENUM_INSTANCE: {
                    AngaraEnumInstanceHeader* e1 = (AngaraEnumInstanceHeader*)AS_OBJ(a);
                    AngaraEnumInstanceHeader* e2 = (AngaraEnumInstanceHeader*)AS_OBJ(b);
                    if (e1->info != e2->info) return angara_create_bool(false);
                    if (e1->info->equals_fn) {
                        return angara_create_bool(e1->info->equals_fn(e1, e2));
                    }
                    return angara_create_bool(e1 == e2);
                }

                default:
                    return angara_create_bool(AS_OBJ(a) == AS_OBJ(b));
            }
        }
    }
    return angara_create_bool(false);
}

// --- Instance Checks ---
AngaraObject angara_is_instance_of(AngaraObject object, const char* type_name) {
    bool result = false;

    switch (object.type) {
        case VAL_NIL:
            result = (strcmp(type_name, "nil") == 0);
            break;
        case VAL_BOOL:
            result = (strcmp(type_name, "bool") == 0);
            break;
        case VAL_I64:
            result = (strcmp(type_name, "i64") == 0 || strcmp(type_name, "int") == 0);
            break;
        case VAL_F64:
            result = (strcmp(type_name, "f64") == 0 || strcmp(type_name, "float") == 0);
            break;
        case VAL_OBJ: {
            switch (OBJ_TYPE(object)) {
                case OBJ_STRING:
                    result = (strcmp(type_name, "string") == 0);
                    break;
                case OBJ_LIST:
                    result = (strcmp(type_name, "list") == 0);
                    break;
                case OBJ_RECORD:
                    result = (strcmp(type_name, "record") == 0);
                    break;
                case OBJ_CLOSURE:
                    result = (strcmp(type_name, "function") == 0);
                    break;
                case OBJ_THREAD:
                    result = (strcmp(type_name, "Thread") == 0);
                    break;
                case OBJ_MUTEX:
                    result = (strcmp(type_name, "Mutex") == 0);
                    break;
                case OBJ_EXCEPTION:
                    result = (strcmp(type_name, "Exception") == 0);
                    break;
                case OBJ_NATIVE_INSTANCE: {
                    AngaraNativeInstance* ni = AS_NATIVE_INSTANCE(object);
                    if (ni->type_name) {
                        result = (strcmp(ni->type_name, type_name) == 0);
                    }
                    break;
                }
                case OBJ_INSTANCE: {
                    AngaraInstance* instance = AS_INSTANCE(object);
                    if (instance->klass && instance->klass->name) {
                        result = (strcmp(instance->klass->name, type_name) == 0);
                    }
                    break;
                }
                case OBJ_DATA_INSTANCE: {
                    AngaraDataInstanceHeader* h = (AngaraDataInstanceHeader*)AS_OBJ(object);
                    if (h->info && h->info->name) {
                        result = (strcmp(h->info->name, type_name) == 0);
                    }
                    break;
                }
                case OBJ_ENUM_INSTANCE: {
                    AngaraEnumInstanceHeader* h = (AngaraEnumInstanceHeader*)AS_OBJ(object);
                    if (h->info && h->info->name) {
                        result = (strcmp(h->info->name, type_name) == 0);
                    }
                    break;
                }
                default:
                    result = false;
                    break;
            }
            break;
        }
    }
    return angara_create_bool(result);
}

AngaraObject angara_is_list_of_type(AngaraObject list_obj, const char* element_type_name) {
    if (!IS_LIST(list_obj)) return angara_create_bool(false);
    AngaraList* list = AS_LIST(list_obj);

    if (list->count == 0) return angara_create_bool(true);

    // Angara lists are homogeneous, so checking the first element is sufficient
    return angara_is_instance_of(list->elements[0], element_type_name);
}

// --- Deep Clone ---
AngaraObject angara_deep_clone(AngaraObject value) {
    if (!IS_OBJ(value)) return value;

    switch (OBJ_TYPE(value)) {
        case OBJ_STRING:
            angara_incref(value);
            return value;

        case OBJ_LIST: {
            AngaraList* src = AS_LIST(value);
            AngaraObject dest_obj = angara_list_new();
            AngaraList* dest = AS_LIST(dest_obj);

            if (src->count > 0) {
                dest->capacity = src->count;
                dest->elements = (AngaraObject*)malloc(sizeof(AngaraObject) * dest->capacity);
            }

            for (size_t i = 0; i < src->count; i++) {
                dest->elements[i] = angara_deep_clone(src->elements[i]);
                dest->count++;
            }
            return dest_obj;
        }

        case OBJ_RECORD: {
            AngaraRecord* src = AS_RECORD(value);
            AngaraObject dest_obj = angara_record_new();

            for (size_t i = 0; i < src->count; i++) {
                AngaraObject val_clone = angara_deep_clone(src->entries[i].value);
                angara_record_set(dest_obj, src->entries[i].key, val_clone);
                angara_decref(val_clone);
            }
            return dest_obj;
        }

        case OBJ_DATA_INSTANCE: {
            AngaraDataInstanceHeader* h = (AngaraDataInstanceHeader*)AS_OBJ(value);
            if (h->info && h->info->deep_clone_fn) {
                return h->info->deep_clone_fn(h);
            }
            angara_throw_error("Cannot deep clone this data type.");
            return angara_create_nil();
        }

        case OBJ_ENUM_INSTANCE: {
            AngaraEnumInstanceHeader* h = (AngaraEnumInstanceHeader*)AS_OBJ(value);
            if (h->info && h->info->deep_clone_fn) {
                return h->info->deep_clone_fn(h);
            }
            angara_throw_error("Cannot deep clone this enum type.");
            return angara_create_nil();
        }

        default:
            // Reference types (Classes, Closures, Threads, Mutexes) — shallow copy
            angara_incref(value);
            return value;
    }
}

void* angara_deep_clone_ptr(void* ptr) {
    if (!ptr) return NULL;
    AngaraObject boxed = BOX_PTR(ptr);
    AngaraObject cloned = angara_deep_clone(boxed);
    if (IS_OBJ(cloned)) return cloned.as.obj;
    return NULL;
}

// --- Generic Container Get ---
AngaraObject angara_get(AngaraObject container, AngaraObject key) {
    if (!IS_OBJ(container)) return angara_create_nil();

    if (OBJ_TYPE(container) == OBJ_LIST && IS_I64(key)) {
        return angara_list_get(container, key);
    }
    if (OBJ_TYPE(container) == OBJ_RECORD && IS_STRING(key)) {
        return angara_record_get_with_angara_key(container, key);
    }

    return angara_create_nil();
}