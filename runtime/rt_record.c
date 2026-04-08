//
// Angara Runtime — Record (dynamic key-value map) operations
//

#include "rt_internal.h"

// --- Internal Capacity Growth ---
void grow_record_capacity(AngaraRecord* record) {
    size_t old_capacity = record->capacity;
    record->capacity = old_capacity < 8 ? 8 : old_capacity * 2;
    record->entries = (RecordEntry*)realloc(record->entries, sizeof(RecordEntry) * record->capacity);
    if (record->entries == NULL) {
        fprintf(stderr, "Out of memory: failed to grow record capacity.\n");
        exit(1);
    }
}

// --- Record Constructor ---
AngaraObject angara_record_new(void) {
    AngaraRecord* record = (AngaraRecord*)malloc(sizeof(AngaraRecord));
    if (record == NULL) {
        fprintf(stderr, "Out of memory: failed to allocate record.\n");
        exit(1);
    }
    record->obj.type = OBJ_RECORD;
    record->obj.ref_count = 1;
    record->count = 0;
    record->capacity = 0;
    record->entries = NULL;
    return (AngaraObject){VAL_OBJ, {.obj = (Object*)record}};
}

// --- Record Get/Set (C string key) ---
void angara_record_set(AngaraObject record_obj, const char* key, AngaraObject value) {
    if (!IS_OBJ(record_obj) || OBJ_TYPE(record_obj) != OBJ_RECORD) return;
    AngaraRecord* record = AS_RECORD(record_obj);

    // Check if the key already exists (linear search)
    for (size_t i = 0; i < record->count; i++) {
        if (strcmp(record->entries[i].key, key) == 0) {
            angara_decref(record->entries[i].value);
            record->entries[i].value = value;
            angara_incref(value);
            return;
        }
    }

    // Key not found — add a new entry
    if (record->capacity < record->count + 1) {
        grow_record_capacity(record);
    }

    RecordEntry* entry = &record->entries[record->count];
    record->count++;
    entry->key = strdup(key);
    entry->value = value;
    angara_incref(value);
}

AngaraObject angara_record_get(AngaraObject record_obj, const char* key) {
    if (!IS_OBJ(record_obj) || OBJ_TYPE(record_obj) != OBJ_RECORD) return angara_create_nil();
    AngaraRecord* record = AS_RECORD(record_obj);

    for (size_t i = 0; i < record->count; i++) {
        if (strcmp(record->entries[i].key, key) == 0) {
            angara_incref(record->entries[i].value);
            return record->entries[i].value;
        }
    }
    return angara_create_nil();
}

// --- Record Get/Set (AngaraString key) ---
AngaraObject angara_record_get_with_angara_key(AngaraObject record_obj, AngaraObject key_obj) {
    if (!IS_STRING(key_obj)) return angara_create_nil();
    return angara_record_get(record_obj, AS_CSTRING(key_obj));
}

void angara_record_set_with_angara_key(AngaraObject record_obj, AngaraObject key_obj, AngaraObject value_obj) {
    if (IS_STRING(key_obj)) {
        angara_record_set(record_obj, AS_CSTRING(key_obj), value_obj);
    }
}

// --- Record from literal (transpiler-generated) ---
AngaraObject angara_record_new_with_fields(size_t pair_count, AngaraObject kvs[]) {
    AngaraObject record_obj = angara_record_new();

    for (size_t i = 0; i < pair_count; i++) {
        AngaraObject key_obj = kvs[i * 2];
        AngaraObject value_obj = kvs[i * 2 + 1];
        const char* key_cstr = AS_CSTRING(key_obj);
        angara_record_set(record_obj, key_cstr, value_obj);
    }

    return record_obj;
}

// --- Record Remove ---
AngaraObject angara_record_remove(AngaraObject record_obj, AngaraObject key_obj) {
    if (!IS_RECORD(record_obj) || !IS_STRING(key_obj)) {
        return angara_create_bool(false);
    }
    AngaraRecord* record = AS_RECORD(record_obj);
    const char* key_to_remove = AS_CSTRING(key_obj);

    int64_t found_index = -1;
    for (size_t i = 0; i < record->count; ++i) {
        if (strcmp(record->entries[i].key, key_to_remove) == 0) {
            found_index = (int64_t)i;
            break;
        }
    }

    if (found_index == -1) {
        return angara_create_bool(false);
    }

    free(record->entries[found_index].key);
    angara_decref(record->entries[found_index].value);

    if (record->count > 1 && (size_t)found_index < record->count - 1) {
        memmove(&record->entries[found_index],
                &record->entries[found_index + 1],
                (record->count - found_index - 1) * sizeof(RecordEntry));
    }

    record->count--;
    return angara_create_bool(true);
}

// --- Record Keys ---
AngaraObject angara_record_keys(AngaraObject record_obj) {
    if (!IS_RECORD(record_obj)) {
        return angara_list_new();
    }
    AngaraRecord* record = AS_RECORD(record_obj);

    AngaraObject keys_list = angara_list_new();
    for (size_t i = 0; i < record->count; ++i) {
        angara_list_push(keys_list, angara_create_string(record->entries[i].key));
    }

    return keys_list;
}

// --- Record Clone ---
AngaraObject angara_record_clone(AngaraObject record_obj) {
    if (!IS_RECORD(record_obj)) return angara_create_nil();
    AngaraRecord* src = AS_RECORD(record_obj);

    AngaraObject dest_obj = angara_record_new();
    AngaraRecord* dest = AS_RECORD(dest_obj);

    // Pre-allocate capacity to match source
    if (src->count > 0) {
        dest->entries = (RecordEntry*)malloc(sizeof(RecordEntry) * src->count);
        if (dest->entries == NULL) {
            free(dest);
            return angara_create_nil();
        }
        dest->capacity = src->count;
    }

    // Shallow copy entries
    for (size_t i = 0; i < src->count; ++i) {
        dest->entries[i].key = strdup(src->entries[i].key);
        dest->entries[i].value = src->entries[i].value;
        angara_incref(dest->entries[i].value);
        dest->count++;
    }

    return dest_obj;
}