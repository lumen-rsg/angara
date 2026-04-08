//
// Angara Runtime — List operations
//

#include "rt_internal.h"

// --- Internal Capacity Growth ---
void grow_list_capacity(AngaraList* list) {
    size_t old_capacity = list->capacity;
    list->capacity = old_capacity < 8 ? 8 : old_capacity * 2;
    list->elements = (AngaraObject*)realloc(list->elements, sizeof(AngaraObject) * list->capacity);
}

// --- List Constructors ---
AngaraObject angara_list_new(void) {
    AngaraList* list = (AngaraList*)malloc(sizeof(AngaraList));
    list->obj.type = OBJ_LIST;
    list->obj.ref_count = 1;
    list->count = 0;
    list->capacity = 0;
    list->elements = NULL;
    return (AngaraObject){VAL_OBJ, {.obj = (Object*)list}};
}

AngaraObject angara_list_new_with_elements(size_t count, AngaraObject elements[]) {
    AngaraObject list_obj = angara_list_new();
    for (size_t i = 0; i < count; i++) {
        angara_list_push(list_obj, elements[i]);
    }
    return list_obj;
}

// --- List Operations ---
void angara_list_push(AngaraObject list_obj, AngaraObject value) {
    AngaraList* list = AS_LIST(list_obj);
    if (list->capacity < list->count + 1) grow_list_capacity(list);
    list->elements[list->count] = value;
    list->count++;
    angara_incref(value);
}

AngaraObject angara_list_get(AngaraObject list_obj, AngaraObject index_obj) {
    AngaraList* list = AS_LIST(list_obj);
    int64_t index = AS_I64(index_obj);
    if (index < 0 || index >= (int64_t)list->count) return angara_create_nil();
    AngaraObject value = list->elements[index];
    angara_incref(value);
    return value;
}

void angara_list_set(AngaraObject list_obj, AngaraObject index_obj, AngaraObject value) {
    AngaraList* list = AS_LIST(list_obj);
    int64_t index = AS_I64(index_obj);
    if (index < 0 || index >= (int64_t)list->count) return;
    angara_decref(list->elements[index]);
    list->elements[index] = value;
    angara_incref(value);
}

AngaraObject angara_list_remove_at(AngaraObject list_obj, AngaraObject index_obj) {
    if (!IS_LIST(list_obj) || !IS_I64(index_obj)) {
        angara_throw_error("remove_at(list, index) received invalid arguments.");
        return angara_create_nil();
    }
    AngaraList* list = AS_LIST(list_obj);
    int64_t index = AS_I64(index_obj);

    if (index < 0 || (size_t)index >= list->count) {
        angara_throw_error("List index out of bounds for remove_at().");
        return angara_create_nil();
    }

    AngaraObject removed_value = list->elements[index];

    if (list->count > 1 && (size_t)index < list->count - 1) {
        memmove(&list->elements[index],
                &list->elements[index + 1],
                (list->count - index - 1) * sizeof(AngaraObject));
    }

    list->count--;
    return removed_value;
}

AngaraObject angara_list_remove(AngaraObject list_obj, AngaraObject value_to_remove) {
    if (!IS_LIST(list_obj)) {
        angara_throw_error("remove(list, value) received invalid list argument.");
        return angara_create_nil();
    }
    AngaraList* list = AS_LIST(list_obj);

    int64_t found_index = -1;
    for (size_t i = 0; i < list->count; ++i) {
        if (AS_BOOL(angara_equals(list->elements[i], value_to_remove))) {
            found_index = (int64_t)i;
            break;
        }
    }

    if (found_index != -1) {
        AngaraObject removed = angara_list_remove_at(list_obj, angara_create_i64(found_index));
        angara_decref(removed);
        return angara_create_bool(true);
    }

    return angara_create_bool(false);
}

// --- Raw C API for Lists ---
void* angara_list_get_raw(void* list, int64_t index) {
    AngaraList* l = (AngaraList*)list;
    if (index < 0 || index >= (int64_t)l->count) return NULL;
    AngaraObject val = l->elements[index];
    if (IS_OBJ(val)) {
        angara_incref(val);
        return val.as.obj;
    }
    return (void*)(intptr_t)val.as.i64;
}

void angara_list_push_raw(void* list, void* item) {
    AngaraObject obj = angara_from_c_object(item);
    angara_list_push(obj, angara_create_nil()); // placeholder; real impl depends on transpiler context
}