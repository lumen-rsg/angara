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

// join(["a", "b"], ", ") -> "a, b"
AngaraObject Angara_adv_string_join(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_LIST(args[0]) || !IS_STRING(args[1])) {
        return angara_create_nil();
    }

    AngaraList* list = AS_LIST(args[0]);
    const char* sep = AS_CSTRING(args[1]);
    size_t sep_len = strlen(sep);

    if (list->count == 0) return angara_string_from_c("");

    // 1. Calculate total length
    size_t total_len = 0;
    for (size_t i = 0; i < list->count; i++) {
        if (IS_STRING(list->elements[i])) {
            total_len += strlen(AS_CSTRING(list->elements[i]));
        }
        if (i < list->count - 1) total_len += sep_len;
    }

    // 2. Allocate
    char* result = malloc(total_len + 1);
    char* ptr = result;

    // 3. Build
    for (size_t i = 0; i < list->count; i++) {
        if (IS_STRING(list->elements[i])) {
            const char* s = AS_CSTRING(list->elements[i]);
            size_t len = strlen(s);
            memcpy(ptr, s, len);
            ptr += len;
        }
        if (i < list->count - 1) {
            memcpy(ptr, sep, sep_len);
            ptr += sep_len;
        }
    }
    *ptr = '\0';

    return angara_create_string_no_copy(result, total_len);
}

// replace(source, search, replacement) -> string
AngaraObject Angara_adv_string_replace(int arg_count, AngaraObject* args) {
    if (arg_count != 3 || !IS_STRING(args[0]) || !IS_STRING(args[1]) || !IS_STRING(args[2])) {
        angara_throw_error("string.replace expects (source: string, search: string, replacement: string).");
        return angara_create_nil();
    }

    const char* source = AS_CSTRING(args[0]);
    const char* search = AS_CSTRING(args[1]);
    const char* replacement = AS_CSTRING(args[2]);

    size_t source_len = strlen(source);
    size_t search_len = strlen(search);
    size_t replacement_len = strlen(replacement);

    // Edge Case: Search string is empty.
    // Standard behavior varies, but usually, we return the original string
    // to avoid infinite loops or inserting replacement between every char.
    if (search_len == 0) {
        angara_incref(args[0]);
        return args[0];
    }

    // --- Pass 1: Count occurrences ---
    int count = 0;
    const char* temp_ptr = source;
    while ((temp_ptr = strstr(temp_ptr, search))) {
        count++;
        temp_ptr += search_len;
    }

    // Optimization: If no occurrences found, return original string.
    if (count == 0) {
        angara_incref(args[0]);
        return args[0];
    }

    // --- Calculate new length ---
    // The new length is: original length + (difference * count)
    // Note: 'diff' can be negative if replacement is shorter than search.
    // We use long long to prevent underflow during calculation before casting back.
    long long len_diff = (long long)replacement_len - (long long)search_len;
    size_t new_len = source_len + (count * len_diff);

    // --- Pass 2: Build the new string ---
    char* result_buffer = (char*)malloc(new_len + 1);
    if (!result_buffer) {
        angara_throw_error("Out of memory during string replacement.");
        return angara_create_nil();
    }

    char* dest_ptr = result_buffer;
    const char* src_ptr = source;
    const char* next_match;

    while (count > 0) {
        // Find next match
        next_match = strstr(src_ptr, search);

        // Copy content BEFORE the match
        size_t segment_len = next_match - src_ptr;
        memcpy(dest_ptr, src_ptr, segment_len);
        dest_ptr += segment_len;

        // Copy REPLACEMENT
        memcpy(dest_ptr, replacement, replacement_len);
        dest_ptr += replacement_len;

        // Advance pointers
        src_ptr = next_match + search_len;
        count--;
    }

    // Copy the remaining part of the string after the last match
    strcpy(dest_ptr, src_ptr);

    // Create the Angara object, transferring ownership of result_buffer
    return angara_create_string_no_copy(result_buffer, new_len);
}

// index_of(haystack, needle) -> i64 (returns -1 if not found)
AngaraObject Angara_adv_string_index_of(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) {
        return angara_create_i64(-1);
    }
    const char* haystack = AS_CSTRING(args[0]);
    const char* needle = AS_CSTRING(args[1]);

    char* found = strstr(haystack, needle);
    if (!found) return angara_create_i64(-1);

    return angara_create_i64((int64_t)(found - haystack));
}

// last_index_of(haystack, needle) -> i64
AngaraObject Angara_adv_string_last_index_of(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) {
        return angara_create_i64(-1);
    }
    const char* haystack = AS_CSTRING(args[0]);
    const char* needle = AS_CSTRING(args[1]);
    size_t haystack_len = strlen(haystack);
    size_t needle_len = strlen(needle);

    if (needle_len > haystack_len) return angara_create_i64(-1);
    if (needle_len == 0) return angara_create_i64((int64_t)haystack_len);

    // Search backwards
    for (long i = (long)(haystack_len - needle_len); i >= 0; --i) {
        if (strncmp(haystack + i, needle, needle_len) == 0) {
            return angara_create_i64(i);
        }
    }
    return angara_create_i64(-1);
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
{"join",          Angara_adv_string_join,          "l<s>s->s", NULL},{"index_of",      Angara_adv_string_index_of,      "ss->i",  NULL}, // New
    {"last_index_of", Angara_adv_string_last_index_of, "ss->i",  NULL}, // New

    // replace(source: string, search: string, replacement: string) -> string
    {"replace",       Angara_adv_string_replace,       "sss->s",   NULL},
        {NULL, NULL, NULL, NULL}
};

ANGARA_MODULE_INIT(adv_string) {
    *def_count = (sizeof(STRING_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return STRING_EXPORTS;
}