//
// Created by cv2 on 9/11/25.
//

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h> // For isdigit, isspace
#include "../runtime/angara_runtime.h"

// --- Function Implementations ---

// Gets the character (as a new string) at a given index.
AngaraObject Angara_adv_string_get(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_STRING(args[0]) || !IS_I64(args[1])) {
        angara_throw_error("get(string, index) expects a string and an integer.");
        return angara_create_nil();
    }
    AngaraString* str = (AngaraString*)args[0].as.obj;
    int64_t index = AS_I64(args[1]);

    if (index < 0 || (size_t)index >= str->length) {
        angara_throw_error("String index out of bounds.");
        return angara_create_nil();
    }

    // Create a new, null-terminated C string of length 1.
    char* char_buf = (char*)malloc(2);
    char_buf[0] = str->chars[index];
    char_buf[1] = '\0';

    return angara_create_string_no_copy(char_buf, 1);
}

// Extracts a substring. Handles bounds checking.
AngaraObject Angara_adv_string_substring(int arg_count, AngaraObject* args) {
    if (arg_count != 3 || !IS_STRING(args[0]) || !IS_I64(args[1]) || !IS_I64(args[2])) {
        angara_throw_error("substring(string, start, end) expects a string and two integers.");
        return angara_create_nil();
    }
    AngaraString* str = (AngaraString*)args[0].as.obj;
    int64_t start = AS_I64(args[1]);
    int64_t end = AS_I64(args[2]);

    if (start < 0 || (size_t)end > str->length || start > end) {
        angara_throw_error("Substring indices are out of bounds or invalid.");
        return angara_create_nil();
    }

    size_t len = end - start;
    char* sub_buf = (char*)malloc(len + 1);
    memcpy(sub_buf, str->chars + start, len);
    sub_buf[len] = '\0';

    return angara_create_string_no_copy(sub_buf, len);
}

// Checks if a single-character string is a digit.
AngaraObject Angara_adv_string_is_digit(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STRING(args[0])) {
        angara_throw_error("is_digit(char) expects a string.");
        return angara_create_nil();
    }
    AngaraString* str = (AngaraString*)args[0].as.obj;
    if (str->length != 1) {
        return angara_create_bool(false);
    }
    return angara_create_bool(isdigit(str->chars[0]));
}

// Checks if a single-character string is whitespace.
AngaraObject Angara_adv_string_is_whitespace(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STRING(args[0])) {
        angara_throw_error("is_whitespace(char) expects a string.");
        return angara_create_nil();
    }
    AngaraString* str = (AngaraString*)args[0].as.obj;
    if (str->length != 1) {
        return angara_create_bool(false);
    }
    return angara_create_bool(isspace(str->chars[0]));
}

// --- C Implementation of pad_end ---
// Angara signature: func pad_end(base as string, length as i64, pad_char as string) -> string
AngaraObject Angara_adv_string_pad_end(int arg_count, AngaraObject* args) {
    // 1. Validate arguments (arity and types).
    if (arg_count != 3) {
        angara_throw_error("pad_end() requires exactly 3 arguments: (string, i64, string).");
        return angara_create_nil(); // Unreachable, but good practice
    }
    if (!IS_STRING(args[0]) || !IS_I64(args[1]) || !IS_STRING(args[2])) {
        angara_throw_error("Invalid argument types for pad_end(string, i64, string).");
        return angara_create_nil();
    }

    // 2. Unbox the Angara arguments into C types.
    const char* base_str = AS_CSTRING(args[0]);
    size_t base_len = AS_STRING(args[0])->length;
    int64_t target_len = AS_I64(args[1]);
    const char* pad_str = AS_CSTRING(args[2]);

    // 3. Perform the logic.
    if ((int64_t)base_len >= target_len) {
        // The string is already long enough. Return a copy of the original.
        angara_incref(args[0]);
        return args[0];
    }

    // The padding character should be the first character of the padding string.
    char pad_char = (AS_STRING(args[2])->length > 0) ? pad_str[0] : ' ';
    size_t pad_count = target_len - base_len;

    // 4. Allocate memory for the new, padded string.
    char* result_buf = (char*)malloc(target_len + 1);
    if (!result_buf) {
        angara_throw_error("Out of memory in pad_end().");
        return angara_create_nil();
    }

    // 5. Build the new string.
    memcpy(result_buf, base_str, base_len);
    memset(result_buf + base_len, pad_char, pad_count);
    result_buf[target_len] = '\0';

    // 6. Box the C string back into an AngaraObject and return it.
    //    The new AngaraString takes ownership of the malloc'd buffer.
    return angara_create_string_no_copy(result_buf, target_len);
}

AngaraObject Angara_adv_string_to_uppercase(int arg_count, AngaraObject args[]) {
    if (arg_count != 1 || !IS_STRING(args[0])) {
        angara_throw_error("adv_string.to_uppercase() requires one string argument.");
        return angara_create_nil();
    }

    const char* source_str = AS_CSTRING(args[0]);
    size_t len = AS_STRING(args[0])->length;

    // 1. Allocate a new buffer for the uppercase string.
    char* new_str = (char*)malloc(len + 1);
    if (!new_str) {
        angara_throw_error("Out of memory in to_uppercase().");
        return angara_create_nil();
    }

    // 2. Iterate through the source and convert each character.
    for (size_t i = 0; i < len; ++i) {
        new_str[i] = toupper((unsigned char)source_str[i]);
    }
    new_str[len] = '\0'; // Null-terminate the new string.

    // 3. Box the new C string into an AngaraObject, giving it ownership of the buffer.
    return angara_create_string_no_copy(new_str, len);
}


// Angara signature: func to_lowercase(s as string) -> string
AngaraObject Angara_adv_string_to_lowercase(int arg_count, AngaraObject args[]) {
    if (arg_count != 1 || !IS_STRING(args[0])) { angara_throw_error("to_lowercase() requires one string argument."); return angara_create_nil(); }
    const char* source_str = AS_CSTRING(args[0]);
    size_t len = AS_STRING(args[0])->length;
    char* new_str = (char*)malloc(len + 1);
    if (!new_str) { angara_throw_error("Out of memory in to_lowercase()."); return angara_create_nil(); }
    for (size_t i = 0; i < len; ++i) { new_str[i] = tolower((unsigned char)source_str[i]); }
    new_str[len] = '\0';
    return angara_create_string_no_copy(new_str, len);
}

// Angara signature: func trim(s as string) -> string
AngaraObject Angara_adv_string_trim(int arg_count, AngaraObject args[]) {
    if (arg_count != 1 || !IS_STRING(args[0])) { angara_throw_error("trim() requires one string argument."); return angara_create_nil(); }
    const char* start = AS_CSTRING(args[0]);
    size_t len = AS_STRING(args[0])->length;
    const char* end = start + len - 1;

    // Find the first non-whitespace character.
    while (isspace((unsigned char)*start) && start < end) { start++; }
    // Find the last non-whitespace character.
    while (isspace((unsigned char)*end) && end > start) { end--; }

    size_t new_len = (end - start) + 1;

    // Use angara_create_string_with_len which copies the substring.
    return angara_create_string_with_len(start, new_len);
}

AngaraObject Angara_adv_string_contains(int arg_count, AngaraObject args[]) {
    if (arg_count != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) {
        angara_throw_error("adv_string.contains() requires two string arguments: (haystack, needle).");
        return angara_create_nil();
    }

    const char* haystack = AS_CSTRING(args[0]);
    const char* needle = AS_CSTRING(args[1]);

    // 1. Use the standard C `strstr` function to search for the substring.
    const char* result = strstr(haystack, needle);

    // 2. If strstr returns a non-NULL pointer, the substring was found.
    //    Return a boxed Angara boolean.
    return angara_create_bool(result != NULL);
}


// --- Module Definition ---


static const AngaraFuncDef STRING_EXPORTS[] = {
        {"get",           Angara_adv_string_get,           "si->s",  NULL},
        {"substring",     Angara_adv_string_substring,     "sii->s", NULL},
        {"is_digit",      Angara_adv_string_is_digit,      "s->b",   NULL},
        {"is_whitespace", Angara_adv_string_is_whitespace, "s->b",   NULL},
        {"pad_end",         Angara_adv_string_pad_end,     "sis->s", NULL},
        {"to_uppercase",  Angara_adv_string_to_uppercase,  "s->s",   NULL},
        {"to_lowercase",  Angara_adv_string_to_lowercase,  "s->s",   NULL},
        {"trim",          Angara_adv_string_trim,          "s->s",   NULL},
        {"contains",      Angara_adv_string_contains,      "ss->b",  NULL},
        {NULL, NULL, NULL, NULL}
};

ANGARA_MODULE_INIT(adv_string) {
    *def_count = (sizeof(STRING_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return STRING_EXPORTS;
}