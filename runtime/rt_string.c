//
// Angara Runtime — String operations and StringBuilder
//

#include "rt_internal.h"

// --- StringBuilder ---
void sb_init(StringBuilder* sb) {
    sb->capacity = 64;
    sb->length = 0;
    sb->buffer = (char*)malloc(sb->capacity);
    sb->buffer[0] = '\0';
}

void sb_append(StringBuilder* sb, const char* str) {
    size_t len = strlen(str);
    if (sb->length + len + 1 >= sb->capacity) {
        while (sb->length + len + 1 >= sb->capacity) {
            sb->capacity *= 2;
        }
        sb->buffer = (char*)realloc(sb->buffer, sb->capacity);
    }
    memcpy(sb->buffer + sb->length, str, len);
    sb->length += len;
    sb->buffer[sb->length] = '\0';
}

AngaraObject sb_to_string_obj(StringBuilder* sb) {
    return angara_create_string_no_copy(sb->buffer, sb->length);
}

// --- String Constructors ---
AngaraObject angara_string_from_c(const char* chars) {
    size_t length = strlen(chars);
    AngaraString* string = (AngaraString*)malloc(sizeof(AngaraString));
    string->obj.type = OBJ_STRING;
    string->obj.ref_count = 1;
    string->length = length;
    string->chars = (char*)malloc(length + 1);
    memcpy(string->chars, chars, length);
    string->chars[length] = '\0';
    return (AngaraObject){VAL_OBJ, {.obj = (Object*)string}};
}

AngaraObject angara_create_string_no_copy(char* chars, size_t length) {
    AngaraString* string = (AngaraString*)malloc(sizeof(AngaraString));
    string->obj.type = OBJ_STRING;
    string->obj.ref_count = 1;
    string->length = length;
    string->chars = chars;
    return (AngaraObject){VAL_OBJ, {.obj = (Object*)string}};
}

AngaraObject angara_create_string(const char* chars) {
    size_t length = strlen(chars);
    char* heap_chars = (char*)malloc(length + 1);
    if (!heap_chars) return angara_create_nil();
    memcpy(heap_chars, chars, length);
    heap_chars[length] = '\0';
    return angara_create_string_no_copy(heap_chars, length);
}

AngaraObject angara_create_string_with_len(const char* chars, size_t length) {
    char* heap_chars = (char*)malloc(length + 1);
    if (!heap_chars) return angara_create_nil();
    memcpy(heap_chars, chars, length);
    heap_chars[length] = '\0';
    return angara_create_string_no_copy(heap_chars, length);
}

// --- String Operations ---
AngaraObject angara_string_concat(AngaraObject a, AngaraObject b) {
    AngaraString* s1 = AS_STRING(a);
    AngaraString* s2 = AS_STRING(b);

    size_t new_len = s1->length + s2->length;
    char* new_chars = (char*)malloc(new_len + 1);
    if (!new_chars) return angara_create_nil();

    memcpy(new_chars, s1->chars, s1->length);
    memcpy(new_chars + s1->length, s2->chars, s2->length);
    new_chars[new_len] = '\0';

    return angara_create_string_no_copy(new_chars, new_len);
}

const char* angara_string_concat_raw(const char* a, const char* b) {
    size_t la = strlen(a), lb = strlen(b);
    char* buf = malloc(la + lb + 1);
    memcpy(buf, a, la);
    memcpy(buf + la, b, lb);
    buf[la + lb] = 0;
    return AS_CSTRING(angara_create_string_no_copy(buf, la + lb));
}