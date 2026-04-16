//
// adv_string.c — Angara advanced string module (rewritten for 16-byte ABI + vtable)
//

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include "Angara.h"

#define IS_STR(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)

// get(string, index) -> string
AngaraObject Angara_adv_string_get(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_STR(args[0]) || !ang_is_i64(args[1])) {
        ang_api->throw_error("get(string, index) expects a string and an integer.");
        return ang_nil();
    }
    size_t len = ang_api->str_len(args[0]);
    int64_t index = ang_as_i64(args[1]);
    if (index < 0 || (size_t)index >= len) { ang_api->throw_error("String index out of bounds."); return ang_nil(); }

    const char* chars = ang_api->as_cstr(args[0]);
    char* buf = (char*)malloc(2);
    buf[0] = chars[index]; buf[1] = '\0';
    return ang_api->string_no_copy(buf, 1);
}

// substring(string, start, end) -> string
AngaraObject Angara_adv_string_substring(int arg_count, AngaraObject* args) {
    if (arg_count != 3 || !IS_STR(args[0]) || !ang_is_i64(args[1]) || !ang_is_i64(args[2])) {
        ang_api->throw_error("substring(string, start, end) expects a string and two integers.");
        return ang_nil();
    }
    size_t len = ang_api->str_len(args[0]);
    int64_t start = ang_as_i64(args[1]);
    int64_t end = ang_as_i64(args[2]);
    if (start < 0 || (size_t)end > len || start > end) { ang_api->throw_error("Substring indices out of bounds."); return ang_nil(); }

    size_t sub_len = end - start;
    const char* chars = ang_api->as_cstr(args[0]);
    char* buf = (char*)malloc(sub_len + 1);
    memcpy(buf, chars + start, sub_len);
    buf[sub_len] = '\0';
    return ang_api->string_no_copy(buf, sub_len);
}

// is_digit(char) -> bool
AngaraObject Angara_adv_string_is_digit(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) { ang_api->throw_error("is_digit(char) expects a string."); return ang_nil(); }
    if (ang_api->str_len(args[0]) != 1) return ang_bool(false);
    return ang_bool(isdigit((unsigned char)ang_api->as_cstr(args[0])[0]));
}

// is_whitespace(char) -> bool
AngaraObject Angara_adv_string_is_whitespace(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) { ang_api->throw_error("is_whitespace(char) expects a string."); return ang_nil(); }
    if (ang_api->str_len(args[0]) != 1) return ang_bool(false);
    return ang_bool(isspace((unsigned char)ang_api->as_cstr(args[0])[0]));
}

// pad_end(base, length, pad_char) -> string
AngaraObject Angara_adv_string_pad_end(int arg_count, AngaraObject* args) {
    if (arg_count != 3 || !IS_STR(args[0]) || !ang_is_i64(args[1]) || !IS_STR(args[2])) {
        ang_api->throw_error("pad_end(string, i64, string) expects (string, i64, string).");
        return ang_nil();
    }
    const char* base = ang_api->as_cstr(args[0]);
    size_t base_len = ang_api->str_len(args[0]);
    int64_t target = ang_as_i64(args[1]);
    const char* pad = ang_api->as_cstr(args[2]);

    if ((int64_t)base_len >= target) { ang_api->incref(args[0]); return args[0]; }
    char pc = (ang_api->str_len(args[2]) > 0) ? pad[0] : ' ';
    size_t pad_count = target - base_len;

    char* buf = (char*)malloc(target + 1);
    memcpy(buf, base, base_len);
    memset(buf + base_len, pc, pad_count);
    buf[target] = '\0';
    return ang_api->string_no_copy(buf, target);
}

// to_uppercase(s) -> string
AngaraObject Angara_adv_string_to_uppercase(int arg_count, AngaraObject args[]) {
    if (arg_count != 1 || !IS_STR(args[0])) { ang_api->throw_error("to_uppercase() requires one string."); return ang_nil(); }
    size_t len = ang_api->str_len(args[0]);
    const char* src = ang_api->as_cstr(args[0]);
    char* buf = (char*)malloc(len + 1);
    for (size_t i = 0; i < len; ++i) buf[i] = toupper((unsigned char)src[i]);
    buf[len] = '\0';
    return ang_api->string_no_copy(buf, len);
}

// to_lowercase(s) -> string
AngaraObject Angara_adv_string_to_lowercase(int arg_count, AngaraObject args[]) {
    if (arg_count != 1 || !IS_STR(args[0])) { ang_api->throw_error("to_lowercase() requires one string."); return ang_nil(); }
    size_t len = ang_api->str_len(args[0]);
    const char* src = ang_api->as_cstr(args[0]);
    char* buf = (char*)malloc(len + 1);
    for (size_t i = 0; i < len; ++i) buf[i] = tolower((unsigned char)src[i]);
    buf[len] = '\0';
    return ang_api->string_no_copy(buf, len);
}

// trim(s) -> string
AngaraObject Angara_adv_string_trim(int arg_count, AngaraObject args[]) {
    if (arg_count != 1 || !IS_STR(args[0])) { ang_api->throw_error("trim() requires one string."); return ang_nil(); }
    const char* start = ang_api->as_cstr(args[0]);
    size_t len = ang_api->str_len(args[0]);
    const char* end = start + len - 1;
    while (isspace((unsigned char)*start) && start < end) start++;
    while (isspace((unsigned char)*end) && end > start) end--;
    size_t new_len = (end - start) + 1;
    return ang_api->string_len(start, new_len);
}

// contains(haystack, needle) -> bool
AngaraObject Angara_adv_string_contains(int arg_count, AngaraObject args[]) {
    if (arg_count != 2 || !IS_STR(args[0]) || !IS_STR(args[1])) {
        ang_api->throw_error("contains(haystack, needle) expects two strings.");
        return ang_nil();
    }
    return ang_bool(strstr(ang_api->as_cstr(args[0]), ang_api->as_cstr(args[1])) != NULL);
}

// join(list, separator) -> string
AngaraObject Angara_adv_string_join(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !ang_is_obj(args[0]) || !IS_STR(args[1])) return ang_nil();

    size_t list_len = ang_api->list_len(args[0]);
    const char* sep = ang_api->as_cstr(args[1]);
    size_t sep_len = ang_api->str_len(args[1]);

    if (list_len == 0) return ang_api->string("");

    // Calculate total length
    size_t total = 0;
    for (size_t i = 0; i < list_len; i++) {
        AngaraObject elem = ang_api->list_get(args[0], (int64_t)i);
        if (IS_STR(elem)) total += ang_api->str_len(elem);
        if (i < list_len - 1) total += sep_len;
        ang_api->decref(elem);
    }

    char* result = (char*)malloc(total + 1);
    char* ptr = result;
    for (size_t i = 0; i < list_len; i++) {
        AngaraObject elem = ang_api->list_get(args[0], (int64_t)i);
        if (IS_STR(elem)) {
            const char* s = ang_api->as_cstr(elem);
            size_t slen = ang_api->str_len(elem);
            memcpy(ptr, s, slen); ptr += slen;
        }
        if (i < list_len - 1) { memcpy(ptr, sep, sep_len); ptr += sep_len; }
        ang_api->decref(elem);
    }
    *ptr = '\0';
    return ang_api->string_no_copy(result, total);
}

// replace(source, search, replacement) -> string
AngaraObject Angara_adv_string_replace(int arg_count, AngaraObject* args) {
    if (arg_count != 3 || !IS_STR(args[0]) || !IS_STR(args[1]) || !IS_STR(args[2])) {
        ang_api->throw_error("replace(source, search, replacement) expects three strings.");
        return ang_nil();
    }
    const char* source = ang_api->as_cstr(args[0]);
    const char* search = ang_api->as_cstr(args[1]);
    const char* repl = ang_api->as_cstr(args[2]);
    size_t source_len = strlen(source);
    size_t search_len = strlen(search);
    size_t repl_len = strlen(repl);

    if (search_len == 0) { ang_api->incref(args[0]); return args[0]; }

    // Count occurrences
    int count = 0;
    const char* p = source;
    while ((p = strstr(p, search))) { count++; p += search_len; }
    if (count == 0) { ang_api->incref(args[0]); return args[0]; }

    size_t new_len = source_len + (count * (repl_len - search_len));
    char* buf = (char*)malloc(new_len + 1);
    char* dst = buf;
    const char* src = source;
    const char* next;
    while (count > 0) {
        next = strstr(src, search);
        size_t seg = next - src;
        memcpy(dst, src, seg); dst += seg;
        memcpy(dst, repl, repl_len); dst += repl_len;
        src = next + search_len;
        count--;
    }
    strcpy(dst, src);
    return ang_api->string_no_copy(buf, new_len);
}

// index_of(haystack, needle) -> i64
AngaraObject Angara_adv_string_index_of(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_STR(args[0]) || !IS_STR(args[1])) return ang_i64(-1);
    const char* found = strstr(ang_api->as_cstr(args[0]), ang_api->as_cstr(args[1]));
    if (!found) return ang_i64(-1);
    return ang_i64((int64_t)(found - ang_api->as_cstr(args[0])));
}

// last_index_of(haystack, needle) -> i64
AngaraObject Angara_adv_string_last_index_of(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_STR(args[0]) || !IS_STR(args[1])) return ang_i64(-1);
    const char* haystack = ang_api->as_cstr(args[0]);
    const char* needle = ang_api->as_cstr(args[1]);
    size_t hlen = strlen(haystack);
    size_t nlen = strlen(needle);
    if (nlen > hlen) return ang_i64(-1);
    if (nlen == 0) return ang_i64((int64_t)hlen);
    for (long i = (long)(hlen - nlen); i >= 0; --i) {
        if (strncmp(haystack + i, needle, nlen) == 0) return ang_i64(i);
    }
    return ang_i64(-1);
}

// split(s, delimiter) -> list<string>
AngaraObject Angara_adv_string_split(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_STR(args[0]) || !IS_STR(args[1])) {
        ang_api->throw_error("split(string, delimiter) expects two strings.");
        return ang_nil();
    }
    const char* src = ang_api->as_cstr(args[0]);
    size_t src_len = ang_api->str_len(args[0]);
    const char* delim = ang_api->as_cstr(args[1]);
    size_t delim_len = ang_api->str_len(args[1]);

    AngaraObject list = ang_api->list_new();

    if (delim_len == 0) {
        // Split into individual characters
        for (size_t i = 0; i < src_len; i++) {
            char buf[2] = { src[i], '\0' };
            ang_api->list_push(list, ang_api->string(buf));
        }
        return list;
    }

    const char* start = src;
    const char* end = src + src_len;
    while (start <= end) {
        const char* found = NULL;
        // Find next delimiter
        if (start + delim_len <= end) {
            const char* search_end = end - delim_len + 1;
            for (const char* p = start; p <= search_end; p++) {
                if (memcmp(p, delim, delim_len) == 0) { found = p; break; }
            }
        }
        if (found) {
            size_t seg_len = found - start;
            ang_api->list_push(list, ang_api->string_len(start, seg_len));
            start = found + delim_len;
        } else {
            ang_api->list_push(list, ang_api->string(start));
            break;
        }
    }
    return list;
}

// starts_with(s, prefix) -> bool
AngaraObject Angara_adv_string_starts_with(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_STR(args[0]) || !IS_STR(args[1])) {
        ang_api->throw_error("starts_with(string, prefix) expects two strings.");
        return ang_nil();
    }
    const char* src = ang_api->as_cstr(args[0]);
    size_t src_len = ang_api->str_len(args[0]);
    const char* prefix = ang_api->as_cstr(args[1]);
    size_t prefix_len = ang_api->str_len(args[1]);
    if (prefix_len > src_len) return ang_bool(false);
    return ang_bool(memcmp(src, prefix, prefix_len) == 0);
}

// ends_with(s, suffix) -> bool
AngaraObject Angara_adv_string_ends_with(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_STR(args[0]) || !IS_STR(args[1])) {
        ang_api->throw_error("ends_with(string, suffix) expects two strings.");
        return ang_nil();
    }
    const char* src = ang_api->as_cstr(args[0]);
    size_t src_len = ang_api->str_len(args[0]);
    const char* suffix = ang_api->as_cstr(args[1]);
    size_t suffix_len = ang_api->str_len(args[1]);
    if (suffix_len > src_len) return ang_bool(false);
    return ang_bool(memcmp(src + src_len - suffix_len, suffix, suffix_len) == 0);
}

// repeat(s, n) -> string
AngaraObject Angara_adv_string_repeat(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_STR(args[0]) || !ang_is_i64(args[1])) {
        ang_api->throw_error("repeat(string, n) expects a string and an integer.");
        return ang_nil();
    }
    const char* src = ang_api->as_cstr(args[0]);
    size_t src_len = ang_api->str_len(args[0]);
    int64_t n = ang_as_i64(args[1]);
    if (n <= 0 || src_len == 0) return ang_api->string("");

    size_t out_len = src_len * (size_t)n;
    char* buf = (char*)malloc(out_len + 1);
    if (!buf) { ang_api->throw_error("repeat: out of memory."); return ang_nil(); }
    char* ptr = buf;
    for (int64_t i = 0; i < n; i++) {
        memcpy(ptr, src, src_len);
        ptr += src_len;
    }
    *ptr = '\0';
    return ang_api->string_no_copy(buf, out_len);
}

// reverse(s) -> string
AngaraObject Angara_adv_string_reverse(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) {
        ang_api->throw_error("reverse(string) expects one string.");
        return ang_nil();
    }
    size_t len = ang_api->str_len(args[0]);
    const char* src = ang_api->as_cstr(args[0]);
    char* buf = (char*)malloc(len + 1);
    for (size_t i = 0; i < len; i++) buf[i] = src[len - 1 - i];
    buf[len] = '\0';
    return ang_api->string_no_copy(buf, len);
}

// count(s, substr) -> i64
AngaraObject Angara_adv_string_count(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_STR(args[0]) || !IS_STR(args[1])) {
        ang_api->throw_error("count(string, substr) expects two strings.");
        return ang_nil();
    }
    const char* haystack = ang_api->as_cstr(args[0]);
    const char* needle = ang_api->as_cstr(args[1]);
    size_t nlen = strlen(needle);
    if (nlen == 0) return ang_i64(0);
    int64_t count = 0;
    const char* p = haystack;
    while ((p = strstr(p, needle)) != NULL) { count++; p += nlen; }
    return ang_i64(count);
}

// pad_start(base, length, pad_char) -> string
AngaraObject Angara_adv_string_pad_start(int arg_count, AngaraObject* args) {
    if (arg_count != 3 || !IS_STR(args[0]) || !ang_is_i64(args[1]) || !IS_STR(args[2])) {
        ang_api->throw_error("pad_start(string, i64, string) expects (string, i64, string).");
        return ang_nil();
    }
    const char* base = ang_api->as_cstr(args[0]);
    size_t base_len = ang_api->str_len(args[0]);
    int64_t target = ang_as_i64(args[1]);
    const char* pad = ang_api->as_cstr(args[2]);

    if ((int64_t)base_len >= target) { ang_api->incref(args[0]); return args[0]; }
    char pc = (ang_api->str_len(args[2]) > 0) ? pad[0] : ' ';
    size_t pad_count = target - base_len;

    char* buf = (char*)malloc(target + 1);
    memset(buf, pc, pad_count);
    memcpy(buf + pad_count, base, base_len);
    buf[target] = '\0';
    return ang_api->string_no_copy(buf, target);
}

// to_i64(s) -> i64
AngaraObject Angara_adv_string_to_i64(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) {
        ang_api->throw_error("to_i64(string) expects one string.");
        return ang_nil();
    }
    const char* s = ang_api->as_cstr(args[0]);
    char* end;
    int64_t val = strtoll(s, &end, 10);
    if (end == s) return ang_nil(); // no conversion
    return ang_i64(val);
}

// to_f64(s) -> f64
AngaraObject Angara_adv_string_to_f64(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) {
        ang_api->throw_error("to_f64(string) expects one string.");
        return ang_nil();
    }
    const char* s = ang_api->as_cstr(args[0]);
    char* end;
    double val = strtod(s, &end);
    if (end == s) return ang_nil(); // no conversion
    return ang_f64(val);
}

// chars(s) -> list<string>
AngaraObject Angara_adv_string_chars(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) {
        ang_api->throw_error("chars(string) expects one string.");
        return ang_nil();
    }
    size_t len = ang_api->str_len(args[0]);
    const char* src = ang_api->as_cstr(args[0]);
    AngaraObject list = ang_api->list_new();
    for (size_t i = 0; i < len; i++) {
        char buf[2] = { src[i], '\0' };
        ang_api->list_push(list, ang_api->string(buf));
    }
    return list;
}

// is_alpha(s) -> bool
AngaraObject Angara_adv_string_is_alpha(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) { ang_api->throw_error("is_alpha(s) expects one string."); return ang_nil(); }
    size_t len = ang_api->str_len(args[0]);
    if (len == 0) return ang_bool(false);
    const char* s = ang_api->as_cstr(args[0]);
    for (size_t i = 0; i < len; i++) {
        if (!isalpha((unsigned char)s[i])) return ang_bool(false);
    }
    return ang_bool(true);
}

// is_alnum(s) -> bool
AngaraObject Angara_adv_string_is_alnum(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) { ang_api->throw_error("is_alnum(s) expects one string."); return ang_nil(); }
    size_t len = ang_api->str_len(args[0]);
    if (len == 0) return ang_bool(false);
    const char* s = ang_api->as_cstr(args[0]);
    for (size_t i = 0; i < len; i++) {
        if (!isalnum((unsigned char)s[i])) return ang_bool(false);
    }
    return ang_bool(true);
}

static const AngaraFuncDef STRING_EXPORTS[] = {
    {"get",           Angara_adv_string_get,           "si->s",    NULL},
    {"substring",     Angara_adv_string_substring,     "sii->s",   NULL},
    {"is_digit",      Angara_adv_string_is_digit,      "s->b",     NULL},
    {"is_whitespace", Angara_adv_string_is_whitespace, "s->b",     NULL},
    {"is_alpha",      Angara_adv_string_is_alpha,      "s->b",     NULL},
    {"is_alnum",      Angara_adv_string_is_alnum,      "s->b",     NULL},
    {"pad_end",       Angara_adv_string_pad_end,       "sis->s",   NULL},
    {"pad_start",     Angara_adv_string_pad_start,     "sis->s",   NULL},
    {"to_uppercase",  Angara_adv_string_to_uppercase,  "s->s",     NULL},
    {"to_lowercase",  Angara_adv_string_to_lowercase,  "s->s",     NULL},
    {"trim",          Angara_adv_string_trim,          "s->s",     NULL},
    {"contains",      Angara_adv_string_contains,      "ss->b",    NULL},
    {"join",          Angara_adv_string_join,          "l<s>s->s", NULL},
    {"split",         Angara_adv_string_split,         "ss->l<s>", NULL},
    {"index_of",      Angara_adv_string_index_of,      "ss->i",    NULL},
    {"last_index_of", Angara_adv_string_last_index_of, "ss->i",    NULL},
    {"replace",       Angara_adv_string_replace,       "sss->s",   NULL},
    {"starts_with",   Angara_adv_string_starts_with,   "ss->b",    NULL},
    {"ends_with",     Angara_adv_string_ends_with,     "ss->b",    NULL},
    {"repeat",        Angara_adv_string_repeat,        "si->s",    NULL},
    {"reverse",       Angara_adv_string_reverse,       "s->s",     NULL},
    {"count",         Angara_adv_string_count,         "ss->i",    NULL},
    {"to_i64",        Angara_adv_string_to_i64,        "s->i",     NULL},
    {"to_f64",        Angara_adv_string_to_f64,        "s->d",     NULL},
    {"chars",         Angara_adv_string_chars,         "s->l<s>",  NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(adv_string) {
    ang_api = api;
    *def_count = (sizeof(STRING_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return STRING_EXPORTS;
}