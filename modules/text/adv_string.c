/// Angara advanced string module — string manipulation operations beyond the builtins.
///
/// UTF-8 aware: get(), substring(), chars(), reverse(), is_alpha(), is_alnum(),
/// to_uppercase(), to_lowercase() all operate on Unicode code points, not raw bytes.
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <stdint.h>
#include "Angara.h"

#define IS_STR(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)

/* =========================================================================
   UTF-8 helpers
   ========================================================================= */

/// Return the number of bytes in the UTF-8 sequence whose leading byte is `c`.
/// Returns 0 for continuation bytes (10xxxxxx) and invalid lead bytes.
static int utf8_seq_len(unsigned char c) {
    if ((c & 0x80) == 0) return 1;       /* 0xxxxxxx */
    if ((c & 0xE0) == 0xC0) return 2;    /* 110xxxxx */
    if ((c & 0xF0) == 0xE0) return 3;    /* 1110xxxx */
    if ((c & 0xF8) == 0xF0) return 4;    /* 11110xxx */
    return 0;                             /* continuation or invalid */
}

/// Decode one UTF-8 code point starting at `s`.  Returns the code point
/// and sets `*advance` to the number of bytes consumed (1–4).
/// Returns 0xFFFD (replacement character) for invalid sequences.
static uint32_t utf8_decode(const unsigned char* s, size_t len, int* advance) {
    if (len == 0) { *advance = 0; return 0; }
    int n = utf8_seq_len(s[0]);
    if (n < 1 || (size_t)n > len) { *advance = 1; return 0xFFFD; }

    uint32_t cp;
    if (n == 1) {
        cp = s[0];
    } else if (n == 2) {
        if ((s[1] & 0xC0) != 0x80) { *advance = 1; return 0xFFFD; }
        cp = ((uint32_t)(s[0] & 0x1F) << 6) | (s[1] & 0x3F);
    } else if (n == 3) {
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) { *advance = 1; return 0xFFFD; }
        cp = ((uint32_t)(s[0] & 0x0F) << 12)
           | ((uint32_t)(s[1] & 0x3F) << 6)
           | (s[2] & 0x3F);
    } else {
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80)
            { *advance = 1; return 0xFFFD; }
        cp = ((uint32_t)(s[0] & 0x07) << 18)
           | ((uint32_t)(s[1] & 0x3F) << 12)
           | ((uint32_t)(s[2] & 0x3F) << 6)
           | (s[3] & 0x3F);
    }
    *advance = n;
    return cp;
}

/// Encode a Unicode code point into `buf` (must have at least 4 bytes).
/// Returns the number of bytes written (1–4), or 0 for invalid code points.
static int utf8_encode(uint32_t cp, char* buf) {
    if (cp < 0x80) {
        buf[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        buf[0] = (char)(0xE0 |  (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 |  (cp & 0x3F));
        return 3;
    }
    if (cp < 0x110000) {
        buf[0] = (char)(0xF0 |  (cp >> 18));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (char)(0x80 |  (cp & 0x3F));
        return 4;
    }
    return 0;
}

/// Count the number of Unicode code points in `s[0..len)`.
static size_t utf8_codepoint_count(const unsigned char* s, size_t len) {
    size_t count = 0;
    size_t i = 0;
    while (i < len) {
        int n = utf8_seq_len(s[i]);
        if (n < 1) n = 1;   /* treat invalid bytes as 1-byte code points */
        i += (size_t)n;
        count++;
    }
    return count;
}

/// Return the byte offset of the `index`-th code point (0-based), or `len` if
/// index >= number of code points.
static size_t utf8_codepoint_offset(const unsigned char* s, size_t len, int64_t index) {
    size_t i = 0;
    int64_t cp = 0;
    while (i < len && cp < index) {
        int n = utf8_seq_len(s[i]);
        if (n < 1) n = 1;
        i += (size_t)n;
        cp++;
    }
    return i;
}


/* =========================================================================
   UTF-8-aware functions (fixed)
   ========================================================================= */

AngaraObject Angara_adv_string_get(int arg_count, AngaraObject* args) {
    const unsigned char* chars = (const unsigned char*)ang_api->as_cstr(args[0]);
    size_t len = ang_api->str_len(args[0]);
    int64_t index = ang_as_i64(args[1]);

    size_t offset = utf8_codepoint_offset(chars, len, index);
    if (offset >= len) {
        ang_api->throw_error("String index out of bounds.");
        return ang_nil();
    }

    int advance = utf8_seq_len(chars[offset]);
    if (advance < 1) advance = 1;

    char* buf = (char*)malloc((size_t)advance + 1);
    memcpy(buf, chars + offset, (size_t)advance);
    buf[advance] = '\0';
    return ang_api->string_no_copy(buf, (size_t)advance);
}

AngaraObject Angara_adv_string_substring(int arg_count, AngaraObject* args) {
    const unsigned char* chars = (const unsigned char*)ang_api->as_cstr(args[0]);
    size_t len = ang_api->str_len(args[0]);
    int64_t start_idx = ang_as_i64(args[1]);
    int64_t end_idx   = ang_as_i64(args[2]);

    size_t cp_count = utf8_codepoint_count(chars, len);
    if (start_idx < 0 || end_idx < start_idx || (size_t)end_idx > cp_count) {
        ang_api->throw_error("Substring indices out of bounds.");
        return ang_nil();
    }

    size_t byte_start = utf8_codepoint_offset(chars, len, start_idx);
    size_t byte_end   = utf8_codepoint_offset(chars, len, end_idx);
    size_t sub_len    = byte_end - byte_start;

    char* buf = (char*)malloc(sub_len + 1);
    memcpy(buf, chars + byte_start, sub_len);
    buf[sub_len] = '\0';
    return ang_api->string_no_copy(buf, sub_len);
}

AngaraObject Angara_adv_string_is_digit(int arg_count, AngaraObject* args) {
    /* ASCII-only: digits are single-byte in UTF-8 so this is already correct */
    if (ang_api->str_len(args[0]) != 1) return ang_bool(false);
    return ang_bool(isdigit((unsigned char)ang_api->as_cstr(args[0])[0]));
}

AngaraObject Angara_adv_string_is_whitespace(int arg_count, AngaraObject* args) {
    if (ang_api->str_len(args[0]) != 1) return ang_bool(false);
    return ang_bool(isspace((unsigned char)ang_api->as_cstr(args[0])[0]));
}

AngaraObject Angara_adv_string_pad_end(int arg_count, AngaraObject* args) {
    const char* base = ang_api->as_cstr(args[0]);
    size_t base_len = ang_api->str_len(args[0]);
    int64_t target = ang_as_i64(args[1]);
    const char* pad = ang_api->as_cstr(args[2]);

    if ((int64_t)base_len >= target) { ang_api->incref(args[0]); return args[0]; }
    char pc = (ang_api->str_len(args[2]) > 0) ? pad[0] : ' ';
    size_t pad_count = (size_t)target - base_len;

    char* buf = (char*)malloc((size_t)target + 1);
    memcpy(buf, base, base_len);
    memset(buf + base_len, pc, pad_count);
    buf[target] = '\0';
    return ang_api->string_no_copy(buf, (size_t)target);
}

AngaraObject Angara_adv_string_to_uppercase(int arg_count, AngaraObject args[]) {
    const unsigned char* src = (const unsigned char*)ang_api->as_cstr(args[0]);
    size_t len = ang_api->str_len(args[0]);

    /* worst case: every codepoint expands from 3→4 bytes (unlikely but safe) */
    size_t cap = len * 2 + 1;
    char* buf = (char*)malloc(cap);
    if (!buf) { ang_api->throw_error("to_uppercase: out of memory."); return ang_nil(); }

    size_t j = 0;
    size_t i = 0;
    while (i < len) {
        int advance;
        uint32_t cp = utf8_decode(src + i, len - i, &advance);
        if (advance < 1) advance = 1;

        uint32_t upper = cp;
        /* simple ASCII + Latin-1 Supplement case folding */
        if (cp >= 0x61 && cp <= 0x7A)        upper = cp - 0x20;   /* a-z → A-Z */
        else if (cp == 0xB5)                  upper = 0x39C;       /* µ → Μ */
        else if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) upper = cp - 0x20; /* à-þ → À-Þ */
        else if (cp >= 0xFF && cp <= 0xFF)    upper = 0x178;       /* ÿ → Ÿ */
        /* full Unicode case folding would need a table; this covers the common cases */

        int wrote = utf8_encode(upper, buf + j);
        if (wrote < 1) { /* fallback: copy original bytes */ 
            memcpy(buf + j, src + i, (size_t)advance);
            j += (size_t)advance;
        } else {
            j += (size_t)wrote;
        }
        i += (size_t)advance;
    }
    buf[j] = '\0';
    return ang_api->string_no_copy(buf, j);
}

AngaraObject Angara_adv_string_to_lowercase(int arg_count, AngaraObject args[]) {
    const unsigned char* src = (const unsigned char*)ang_api->as_cstr(args[0]);
    size_t len = ang_api->str_len(args[0]);

    size_t cap = len * 2 + 1;
    char* buf = (char*)malloc(cap);
    if (!buf) { ang_api->throw_error("to_lowercase: out of memory."); return ang_nil(); }

    size_t j = 0;
    size_t i = 0;
    while (i < len) {
        int advance;
        uint32_t cp = utf8_decode(src + i, len - i, &advance);
        if (advance < 1) advance = 1;

        uint32_t lower = cp;
        if (cp >= 0x41 && cp <= 0x5A)        lower = cp + 0x20;   /* A-Z → a-z */
        else if (cp >= 0xC0 && cp <= 0xDE)   lower = cp + 0x20;   /* À-Þ → à-þ */
        else if (cp == 0x178)                 lower = 0xFF;        /* Ÿ → ÿ */

        int wrote = utf8_encode(lower, buf + j);
        if (wrote < 1) {
            memcpy(buf + j, src + i, (size_t)advance);
            j += (size_t)advance;
        } else {
            j += (size_t)wrote;
        }
        i += (size_t)advance;
    }
    buf[j] = '\0';
    return ang_api->string_no_copy(buf, j);
}

AngaraObject Angara_adv_string_trim(int arg_count, AngaraObject args[]) {
    const char* start = ang_api->as_cstr(args[0]);
    size_t len = ang_api->str_len(args[0]);
    const char* end = start + len - 1;
    while (isspace((unsigned char)*start) && start < end) start++;
    while (isspace((unsigned char)*end) && end > start) end--;
    size_t new_len = (size_t)(end - start) + 1;
    return ang_api->string_len(start, new_len);
}

AngaraObject Angara_adv_string_contains(int arg_count, AngaraObject args[]) {
    return ang_bool(strstr(ang_api->as_cstr(args[0]), ang_api->as_cstr(args[1])) != NULL);
}

AngaraObject Angara_adv_string_join(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !ang_is_obj(args[0]) || !IS_STR(args[1])) return ang_nil();

    size_t list_len = ang_api->list_len(args[0]);
    const char* sep = ang_api->as_cstr(args[1]);
    size_t sep_len = ang_api->str_len(args[1]);

    if (list_len == 0) return ang_api->string("");

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

AngaraObject Angara_adv_string_replace(int arg_count, AngaraObject* args) {
    const char* source = ang_api->as_cstr(args[0]);
    const char* search = ang_api->as_cstr(args[1]);
    const char* repl = ang_api->as_cstr(args[2]);
    size_t source_len = strlen(source);
    size_t search_len = strlen(search);
    size_t repl_len = strlen(repl);

    if (search_len == 0) { ang_api->incref(args[0]); return args[0]; }

    int count = 0;
    const char* p = source;
    while ((p = strstr(p, search))) { count++; p += search_len; }
    if (count == 0) { ang_api->incref(args[0]); return args[0]; }

    size_t new_len = source_len + (size_t)count * (repl_len - search_len);
    char* buf = (char*)malloc(new_len + 1);
    char* dst = buf;
    const char* src = source;
    const char* next;
    while (count > 0) {
        next = strstr(src, search);
        size_t seg = (size_t)(next - src);
        memcpy(dst, src, seg); dst += seg;
        memcpy(dst, repl, repl_len); dst += repl_len;
        src = next + search_len;
        count--;
    }
    strcpy(dst, src);
    return ang_api->string_no_copy(buf, new_len);
}

AngaraObject Angara_adv_string_index_of(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_STR(args[0]) || !IS_STR(args[1])) return ang_i64(-1);
    const char* found = strstr(ang_api->as_cstr(args[0]), ang_api->as_cstr(args[1]));
    if (!found) return ang_i64(-1);
    return ang_i64((int64_t)(found - ang_api->as_cstr(args[0])));
}

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

AngaraObject Angara_adv_string_split(int arg_count, AngaraObject* args) {
    const char* src = ang_api->as_cstr(args[0]);
    size_t src_len = ang_api->str_len(args[0]);
    const char* delim = ang_api->as_cstr(args[1]);
    size_t delim_len = ang_api->str_len(args[1]);

    AngaraObject list = ang_api->list_new();

    if (delim_len == 0) {
        /* empty delimiter → split into individual code points */
        const unsigned char* usrc = (const unsigned char*)src;
        size_t i = 0;
        while (i < src_len) {
            int advance;
            utf8_decode(usrc + i, src_len - i, &advance);
            if (advance < 1) advance = 1;
            char* chunk = (char*)malloc((size_t)advance + 1);
            memcpy(chunk, usrc + i, (size_t)advance);
            chunk[advance] = '\0';
            ang_api->list_push(list, ang_api->string_no_copy(chunk, (size_t)advance));
            i += (size_t)advance;
        }
        return list;
    }

    const char* start = src;
    const char* end = src + src_len;
    while (start <= end) {
        const char* found = NULL;
        if (start + delim_len <= end) {
            const char* search_end = end - delim_len + 1;
            for (const char* p = start; p <= search_end; p++) {
                if (memcmp(p, delim, delim_len) == 0) { found = p; break; }
            }
        }
        if (found) {
            size_t seg_len = (size_t)(found - start);
            ang_api->list_push(list, ang_api->string_len(start, seg_len));
            start = found + delim_len;
        } else {
            ang_api->list_push(list, ang_api->string(start));
            break;
        }
    }
    return list;
}

AngaraObject Angara_adv_string_starts_with(int arg_count, AngaraObject* args) {
    const char* src = ang_api->as_cstr(args[0]);
    size_t src_len = ang_api->str_len(args[0]);
    const char* prefix = ang_api->as_cstr(args[1]);
    size_t prefix_len = ang_api->str_len(args[1]);
    if (prefix_len > src_len) return ang_bool(false);
    return ang_bool(memcmp(src, prefix, prefix_len) == 0);
}

AngaraObject Angara_adv_string_ends_with(int arg_count, AngaraObject* args) {
    const char* src = ang_api->as_cstr(args[0]);
    size_t src_len = ang_api->str_len(args[0]);
    const char* suffix = ang_api->as_cstr(args[1]);
    size_t suffix_len = ang_api->str_len(args[1]);
    if (suffix_len > src_len) return ang_bool(false);
    return ang_bool(memcmp(src + src_len - suffix_len, suffix, suffix_len) == 0);
}

AngaraObject Angara_adv_string_repeat(int arg_count, AngaraObject* args) {
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

AngaraObject Angara_adv_string_reverse(int arg_count, AngaraObject* args) {
    const unsigned char* src = (const unsigned char*)ang_api->as_cstr(args[0]);
    size_t len = ang_api->str_len(args[0]);

    /* two-pass: first collect codepoint offsets, then reverse */
    size_t cap = 128;
    size_t* offsets = (size_t*)malloc(cap * sizeof(size_t));
    int*    lengths = (int*)   malloc(cap * sizeof(int));
    size_t n_cp = 0;
    size_t i = 0;
    while (i < len) {
        int advance;
        utf8_decode(src + i, len - i, &advance);
        if (advance < 1) advance = 1;
        if (n_cp >= cap) {
            cap *= 2;
            offsets = (size_t*)realloc(offsets, cap * sizeof(size_t));
            lengths = (int*)   realloc(lengths, cap * sizeof(int));
        }
        offsets[n_cp] = i;
        lengths[n_cp] = advance;
        n_cp++;
        i += (size_t)advance;
    }

    char* buf = (char*)malloc(len + 1);
    size_t j = 0;
    for (size_t k = n_cp; k > 0; k--) {
        size_t off = offsets[k - 1];
        int    alen = lengths[k - 1];
        memcpy(buf + j, src + off, (size_t)alen);
        j += (size_t)alen;
    }
    buf[j] = '\0';
    free(offsets);
    free(lengths);
    return ang_api->string_no_copy(buf, j);
}

AngaraObject Angara_adv_string_count(int arg_count, AngaraObject* args) {
    const char* haystack = ang_api->as_cstr(args[0]);
    const char* needle = ang_api->as_cstr(args[1]);
    size_t nlen = strlen(needle);
    if (nlen == 0) return ang_i64(0);
    int64_t count = 0;
    const char* p = haystack;
    while ((p = strstr(p, needle)) != NULL) { count++; p += nlen; }
    return ang_i64(count);
}

AngaraObject Angara_adv_string_pad_start(int arg_count, AngaraObject* args) {
    const char* base = ang_api->as_cstr(args[0]);
    size_t base_len = ang_api->str_len(args[0]);
    int64_t target = ang_as_i64(args[1]);
    const char* pad = ang_api->as_cstr(args[2]);

    if ((int64_t)base_len >= target) { ang_api->incref(args[0]); return args[0]; }
    char pc = (ang_api->str_len(args[2]) > 0) ? pad[0] : ' ';
    size_t pad_count = (size_t)target - base_len;

    char* buf = (char*)malloc((size_t)target + 1);
    memset(buf, pc, pad_count);
    memcpy(buf + pad_count, base, base_len);
    buf[target] = '\0';
    return ang_api->string_no_copy(buf, (size_t)target);
}

AngaraObject Angara_adv_string_to_i64(int arg_count, AngaraObject* args) {
    const char* s = ang_api->as_cstr(args[0]);
    char* end;
    int64_t val = strtoll(s, &end, 10);
    if (end == s) return ang_nil();
    return ang_i64(val);
}

AngaraObject Angara_adv_string_to_f64(int arg_count, AngaraObject* args) {
    const char* s = ang_api->as_cstr(args[0]);
    char* end;
    double val = strtod(s, &end);
    if (end == s) return ang_nil();
    return ang_f64(val);
}

AngaraObject Angara_adv_string_chars(int arg_count, AngaraObject* args) {
    const unsigned char* src = (const unsigned char*)ang_api->as_cstr(args[0]);
    size_t len = ang_api->str_len(args[0]);
    AngaraObject list = ang_api->list_new();
    size_t i = 0;
    while (i < len) {
        int advance;
        utf8_decode(src + i, len - i, &advance);
        if (advance < 1) advance = 1;
        char* chunk = (char*)malloc((size_t)advance + 1);
        memcpy(chunk, src + i, (size_t)advance);
        chunk[advance] = '\0';
        ang_api->list_push(list, ang_api->string_no_copy(chunk, (size_t)advance));
        i += (size_t)advance;
    }
    return list;
}

AngaraObject Angara_adv_string_is_alpha(int arg_count, AngaraObject* args) {
    const unsigned char* s = (const unsigned char*)ang_api->as_cstr(args[0]);
    size_t len = ang_api->str_len(args[0]);
    if (len == 0) return ang_bool(false);

    size_t i = 0;
    while (i < len) {
        int advance;
        uint32_t cp = utf8_decode(s + i, len - i, &advance);
        if (advance < 1) advance = 1;
        /* ASCII letters + Latin-1 Supplement letters */
        if (!((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
              (cp >= 0xC0 && cp <= 0xFF && cp != 0xD7 && cp != 0xF7)))
            return ang_bool(false);
        i += (size_t)advance;
    }
    return ang_bool(true);
}

AngaraObject Angara_adv_string_is_alnum(int arg_count, AngaraObject* args) {
    const unsigned char* s = (const unsigned char*)ang_api->as_cstr(args[0]);
    size_t len = ang_api->str_len(args[0]);
    if (len == 0) return ang_bool(false);

    size_t i = 0;
    while (i < len) {
        int advance;
        uint32_t cp = utf8_decode(s + i, len - i, &advance);
        if (advance < 1) advance = 1;
        if (!((cp >= '0' && cp <= '9') ||
              (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
              (cp >= 0xC0 && cp <= 0xFF && cp != 0xD7 && cp != 0xF7)))
            return ang_bool(false);
        i += (size_t)advance;
    }
    return ang_bool(true);
}

AngaraObject Angara_adv_string_levenshtein(int arg_count, AngaraObject* args) {
    const unsigned char* a = (const unsigned char*)ang_api->as_cstr(args[0]);
    const unsigned char* b = (const unsigned char*)ang_api->as_cstr(args[1]);
    size_t la = ang_api->str_len(args[0]);
    size_t lb = ang_api->str_len(args[1]);

    /* collect codepoints for both strings */
    size_t cap_a = 64, cap_b = 64;
    uint32_t* cpa = (uint32_t*)malloc(cap_a * sizeof(uint32_t));
    uint32_t* cpb = (uint32_t*)malloc(cap_b * sizeof(uint32_t));
    size_t na = 0, nb = 0;

    size_t i = 0;
    while (i < la) {
        int adv;
        uint32_t cp = utf8_decode(a + i, la - i, &adv);
        if (adv < 1) adv = 1;
        if (na >= cap_a) { cap_a *= 2; cpa = (uint32_t*)realloc(cpa, cap_a * sizeof(uint32_t)); }
        cpa[na++] = cp;
        i += (size_t)adv;
    }
    i = 0;
    while (i < lb) {
        int adv;
        uint32_t cp = utf8_decode(b + i, lb - i, &adv);
        if (adv < 1) adv = 1;
        if (nb >= cap_b) { cap_b *= 2; cpb = (uint32_t*)realloc(cpb, cap_b * sizeof(uint32_t)); }
        cpb[nb++] = cp;
        i += (size_t)adv;
    }

    size_t* d = (size_t*)malloc((na + 1) * (nb + 1) * sizeof(size_t));
    if (!d) { free(cpa); free(cpb); return ang_i64(-1); }
    #define D(i,j) d[(i) * (nb + 1) + (j)]

    for (size_t r = 0; r <= na; r++) D(r, 0) = r;
    for (size_t c = 0; c <= nb; c++) D(0, c) = c;

    for (size_t r = 1; r <= na; r++) {
        for (size_t c = 1; c <= nb; c++) {
            size_t cost = (cpa[r-1] == cpb[c-1]) ? 0 : 1;
            size_t del = D(r-1, c) + 1;
            size_t ins = D(r, c-1) + 1;
            size_t sub = D(r-1, c-1) + cost;
            D(r, c) = del < ins ? (del < sub ? del : sub) : (ins < sub ? ins : sub);
        }
    }
    size_t result = D(na, nb);
    #undef D
    free(d);
    free(cpa);
    free(cpb);
    return ang_i64((int64_t)result);
}


/* =========================================================================
   format() — {} placeholder string interpolation
   ========================================================================= */

AngaraObject Angara_adv_string_format(int arg_count, AngaraObject* args) {
    /* args[0] = format string, args[1..] = values to substitute */

    const char* fmt = ang_api->as_cstr(args[0]);
    size_t fmt_len = ang_api->str_len(args[0]);

    /* first pass: calculate output length */
    size_t out_cap = fmt_len + 256;
    char* out = (char*)malloc(out_cap);
    if (!out) { ang_api->throw_error("format: out of memory."); return ang_nil(); }

    size_t out_len = 0;
    int val_idx = 1;  /* args[1] is the first value */

    size_t i = 0;
    while (i < fmt_len) {
        if (fmt[i] == '{' && i + 1 < fmt_len && fmt[i + 1] == '}') {
            /* substitute */
            i += 2;
            if (val_idx < arg_count) {
                AngaraObject str_val = ang_api->to_string(args[val_idx++]);
                const char* s = ang_api->as_cstr(str_val);
                size_t slen = ang_api->str_len(str_val);

                /* ensure capacity */
                if (out_len + slen + 1 >= out_cap) {
                    out_cap = (out_len + slen) * 2 + 1;
                    char* nb = (char*)realloc(out, out_cap);
                    if (!nb) { free(out); ang_api->decref(str_val); return ang_api->string(""); }
                    out = nb;
                }
                memcpy(out + out_len, s, slen);
                out_len += slen;
                ang_api->decref(str_val);
            } else {
                /* not enough args — leave "{}" as-is */
                if (out_len + 2 >= out_cap) {
                    out_cap = out_len * 2 + 4;
                    char* nb = (char*)realloc(out, out_cap);
                    if (!nb) { free(out); return ang_api->string(""); }
                    out = nb;
                }
                out[out_len++] = '{';
                out[out_len++] = '}';
            }
        } else if (fmt[i] == '{' && i + 1 < fmt_len && fmt[i + 1] == '{') {
            /* escaped "{{" → literal "{" */
            i += 2;
            if (out_len + 1 >= out_cap) {
                out_cap = out_len * 2 + 4;
                char* nb = (char*)realloc(out, out_cap);
                if (!nb) { free(out); return ang_api->string(""); }
                out = nb;
            }
            out[out_len++] = '{';
        } else if (fmt[i] == '}' && i + 1 < fmt_len && fmt[i + 1] == '}') {
            /* escaped "}}" → literal "}" */
            i += 2;
            if (out_len + 1 >= out_cap) {
                out_cap = out_len * 2 + 4;
                char* nb = (char*)realloc(out, out_cap);
                if (!nb) { free(out); return ang_api->string(""); }
                out = nb;
            }
            out[out_len++] = '}';
        } else {
            /* literal character */
            if (out_len + 1 >= out_cap) {
                out_cap = out_len * 2 + 1;
                char* nb = (char*)realloc(out, out_cap);
                if (!nb) { free(out); return ang_api->string(""); }
                out = nb;
            }
            out[out_len++] = fmt[i++];
        }
    }

    out[out_len] = '\0';
    return ang_api->string_no_copy(out, out_len);
}


/* =========================================================================
   Export table
   ========================================================================= */

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
    {"levenshtein",   Angara_adv_string_levenshtein,   "ss->i",    NULL},
    {"format",        Angara_adv_string_format,        "sa*->s",   NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(adv_string) {
    ang_api = api;
    *def_count = (sizeof(STRING_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return STRING_EXPORTS;
}
