/// Angara encoding module — base64, hex, and URL encoding/decoding.
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "Angara.h"

#define IS_STR(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)


static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

AngaraObject Angara_encoding_base64_encode(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) {
        ang_api->throw_error("base64_encode(data) expects one string argument.");
        return ang_nil();
    }
    const unsigned char* src = (const unsigned char*)ang_api->as_cstr(args[0]);
    size_t src_len = ang_api->str_len(args[0]);

    size_t out_len = 4 * ((src_len + 2) / 3);
    char* out = (char*)malloc(out_len + 1);
    if (!out) { ang_api->throw_error("base64_encode: out of memory."); return ang_nil(); }

    size_t j = 0;
    for (size_t i = 0; i < src_len; i += 3) {
        unsigned int n = (unsigned int)src[i] << 16;
        if (i + 1 < src_len) n |= (unsigned int)src[i + 1] << 8;
        if (i + 2 < src_len) n |= (unsigned int)src[i + 2];

        out[j++] = b64_table[(n >> 18) & 0x3F];
        out[j++] = b64_table[(n >> 12) & 0x3F];
        out[j++] = (i + 1 < src_len) ? b64_table[(n >> 6) & 0x3F] : '=';
        out[j++] = (i + 2 < src_len) ? b64_table[n & 0x3F] : '=';
    }
    out[j] = '\0';
    return ang_api->string_no_copy(out, j);
}

static int b64_decode_char(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

AngaraObject Angara_encoding_base64_decode(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) {
        ang_api->throw_error("base64_decode(s) expects one string argument.");
        return ang_nil();
    }
    const char* src = ang_api->as_cstr(args[0]);
    size_t src_len = ang_api->str_len(args[0]);

    if (src_len == 0) return ang_api->string("");

    size_t pad = 0;
    while (src_len > 0 && src[src_len - 1] == '=') { src_len--; pad++; }

    size_t out_len = (src_len * 3) / 4;
    char* out = (char*)malloc(out_len + 1);
    if (!out) { ang_api->throw_error("base64_decode: out of memory."); return ang_nil(); }

    size_t j = 0;
    for (size_t i = 0; i < src_len; i += 4) {
        int a = b64_decode_char(src[i]);
        int b = (i + 1 < src_len) ? b64_decode_char(src[i + 1]) : 0;
        int c = (i + 2 < src_len) ? b64_decode_char(src[i + 2]) : 0;
        int d = (i + 3 < src_len) ? b64_decode_char(src[i + 3]) : 0;

        if (a < 0 || b < 0 || c < 0 || d < 0) {
            free(out);
            ang_api->throw_error("base64_decode: invalid character in input.");
            return ang_nil();
        }

        unsigned int n = ((unsigned int)a << 18) | ((unsigned int)b << 12) | ((unsigned int)c << 6) | (unsigned int)d;
        if (j < out_len) out[j++] = (char)((n >> 16) & 0xFF);
        if (j < out_len) out[j++] = (char)((n >> 8) & 0xFF);
        if (j < out_len) out[j++] = (char)(n & 0xFF);
    }
    out[j] = '\0';
    return ang_api->string_no_copy(out, j);
}


AngaraObject Angara_encoding_hex_encode(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) {
        ang_api->throw_error("hex_encode(data) expects one string argument.");
        return ang_nil();
    }
    const unsigned char* src = (const unsigned char*)ang_api->as_cstr(args[0]);
    size_t len = ang_api->str_len(args[0]);

    char* out = (char*)malloc(len * 2 + 1);
    if (!out) { ang_api->throw_error("hex_encode: out of memory."); return ang_nil(); }

    static const char hex_chars[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2] = hex_chars[(src[i] >> 4) & 0x0F];
        out[i * 2 + 1] = hex_chars[src[i] & 0x0F];
    }
    out[len * 2] = '\0';
    return ang_api->string_no_copy(out, len * 2);
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

AngaraObject Angara_encoding_hex_decode(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) {
        ang_api->throw_error("hex_decode(s) expects one string argument.");
        return ang_nil();
    }
    size_t len = ang_api->str_len(args[0]);
    if (len % 2 != 0) {
        ang_api->throw_error("hex_decode: input length must be even.");
        return ang_nil();
    }
    const char* src = ang_api->as_cstr(args[0]);
    size_t out_len = len / 2;
    char* out = (char*)malloc(out_len + 1);
    if (!out) { ang_api->throw_error("hex_decode: out of memory."); return ang_nil(); }

    for (size_t i = 0; i < out_len; i++) {
        int hi = hex_val(src[i * 2]);
        int lo = hex_val(src[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            free(out);
            ang_api->throw_error("hex_decode: invalid hex character.");
            return ang_nil();
        }
        out[i] = (char)((hi << 4) | lo);
    }
    out[out_len] = '\0';
    return ang_api->string_no_copy(out, out_len);
}


static int is_url_safe(char c) {
    return isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~';
}

AngaraObject Angara_encoding_url_encode(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) {
        ang_api->throw_error("url_encode(s) expects one string argument.");
        return ang_nil();
    }
    const char* src = ang_api->as_cstr(args[0]);
    size_t len = ang_api->str_len(args[0]);

    char* out = (char*)malloc(len * 3 + 1);
    if (!out) { ang_api->throw_error("url_encode: out of memory."); return ang_nil(); }

    static const char hex_chars[] = "0123456789ABCDEF";
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (is_url_safe(src[i])) {
            out[j++] = src[i];
        } else {
            out[j++] = '%';
            out[j++] = hex_chars[((unsigned char)src[i] >> 4) & 0x0F];
            out[j++] = hex_chars[(unsigned char)src[i] & 0x0F];
        }
    }
    out[j] = '\0';
    return ang_api->string_no_copy(out, j);
}

AngaraObject Angara_encoding_url_decode(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) {
        ang_api->throw_error("url_decode(s) expects one string argument.");
        return ang_nil();
    }
    const char* src = ang_api->as_cstr(args[0]);
    size_t len = ang_api->str_len(args[0]);

    char* out = (char*)malloc(len + 1);
    if (!out) { ang_api->throw_error("url_decode: out of memory."); return ang_nil(); }

    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (src[i] == '%' && i + 2 < len) {
            int hi = hex_val(src[i + 1]);
            int lo = hex_val(src[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out[j++] = (char)((hi << 4) | lo);
                i += 2;
                continue;
            }
        } else if (src[i] == '+') {
            out[j++] = ' ';
            continue;
        }
        out[j++] = src[i];
    }
    out[j] = '\0';
    return ang_api->string_no_copy(out, j);
}


static const char b32_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

AngaraObject Angara_encoding_base32_encode(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) {
        ang_api->throw_error("base32_encode(data) expects one string argument.");
        return ang_nil();
    }
    const unsigned char* src = (const unsigned char*)ang_api->as_cstr(args[0]);
    size_t src_len = ang_api->str_len(args[0]);

    size_t out_len = ((src_len + 4) / 5) * 8;
    char* out = (char*)malloc(out_len + 1);
    if (!out) { ang_api->throw_error("base32_encode: out of memory."); return ang_nil(); }

    size_t j = 0;
    for (size_t i = 0; i < src_len; i += 5) {
        uint64_t buf = 0;
        int n_bytes = 0;
        for (int b = 0; b < 5 && i + b < src_len; b++) {
            buf = (buf << 8) | src[i + b];
            n_bytes++;
        }
        int n_chars = ((n_bytes * 8) + 4) / 5;
        buf <<= (5 - n_bytes) * 8 + (5 - n_bytes);
        buf <<= (size_t)(40 - n_bytes * 8);
        size_t bits = (size_t)n_bytes * 8;
        buf <<= (40 - bits);
        for (int c = 0; c < 8; c++) {
            if (c < n_chars) {
                out[j++] = b32_table[(buf >> (35 - c * 5)) & 0x1F];
            } else {
                out[j++] = '=';
            }
        }
    }
    out[j] = '\0';
    return ang_api->string_no_copy(out, j);
}

static int b32_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a';
    if (c >= '2' && c <= '7') return c - '2' + 26;
    return -1;
}

AngaraObject Angara_encoding_base32_decode(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) {
        ang_api->throw_error("base32_decode(s) expects one string argument.");
        return ang_nil();
    }
    const char* src = ang_api->as_cstr(args[0]);
    size_t src_len = ang_api->str_len(args[0]);

    if (src_len == 0) return ang_api->string("");

    size_t out_cap = (src_len * 5) / 8 + 1;
    char* out = (char*)malloc(out_cap);
    if (!out) { ang_api->throw_error("base32_decode: out of memory."); return ang_nil(); }

    size_t buf = 0, bits = 0, j = 0;
    for (size_t i = 0; i < src_len; i++) {
        if (src[i] == '=') break;
        int v = b32_val(src[i]);
        if (v < 0) { free(out); ang_api->throw_error("base32_decode: invalid character."); return ang_nil(); }
        buf = (buf << 5) | (size_t)v;
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            out[j++] = (char)((buf >> bits) & 0xFF);
        }
    }
    out[j] = '\0';
    return ang_api->string_no_copy(out, j);
}


static const char b64url_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

AngaraObject Angara_encoding_base64url_encode(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) {
        ang_api->throw_error("base64url_encode(data) expects one string argument.");
        return ang_nil();
    }
    const unsigned char* src = (const unsigned char*)ang_api->as_cstr(args[0]);
    size_t src_len = ang_api->str_len(args[0]);

    size_t out_len = 4 * ((src_len + 2) / 3);
    char* out = (char*)malloc(out_len + 1);
    if (!out) { ang_api->throw_error("base64url_encode: out of memory."); return ang_nil(); }

    size_t j = 0;
    for (size_t i = 0; i < src_len; i += 3) {
        unsigned int n = (unsigned int)src[i] << 16;
        if (i + 1 < src_len) n |= (unsigned int)src[i + 1] << 8;
        if (i + 2 < src_len) n |= (unsigned int)src[i + 2];

        out[j++] = b64url_table[(n >> 18) & 0x3F];
        out[j++] = b64url_table[(n >> 12) & 0x3F];
        if (i + 1 < src_len) out[j++] = b64url_table[(n >> 6) & 0x3F];
        if (i + 2 < src_len) out[j++] = b64url_table[n & 0x3F];
    }
    out[j] = '\0';
    return ang_api->string_no_copy(out, j);
}

static int b64url_decode_char(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-') return 62;
    if (c == '_') return 63;
    return -1;
}

AngaraObject Angara_encoding_base64url_decode(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_STR(args[0])) {
        ang_api->throw_error("base64url_decode(s) expects one string argument.");
        return ang_nil();
    }
    const char* src = ang_api->as_cstr(args[0]);
    size_t src_len = ang_api->str_len(args[0]);

    if (src_len == 0) return ang_api->string("");

    size_t out_len = (src_len * 3) / 4;
    char* out = (char*)malloc(out_len + 1);
    if (!out) { ang_api->throw_error("base64url_decode: out of memory."); return ang_nil(); }

    size_t j = 0;
    for (size_t i = 0; i < src_len; i += 4) {
        int a = b64url_decode_char(src[i]);
        int b = (i + 1 < src_len) ? b64url_decode_char(src[i + 1]) : 0;
        int c = (i + 2 < src_len) ? b64url_decode_char(src[i + 2]) : 0;
        int d = (i + 3 < src_len) ? b64url_decode_char(src[i + 3]) : 0;

        if (a < 0 || b < 0 || c < 0 || d < 0) {
            free(out);
            ang_api->throw_error("base64url_decode: invalid character in input.");
            return ang_nil();
        }

        unsigned int n = ((unsigned int)a << 18) | ((unsigned int)b << 12) | ((unsigned int)c << 6) | (unsigned int)d;
        if (j < out_len) out[j++] = (char)((n >> 16) & 0xFF);
        if (j < out_len) out[j++] = (char)((n >> 8) & 0xFF);
        if (j < out_len) out[j++] = (char)(n & 0xFF);
    }
    out[j] = '\0';
    return ang_api->string_no_copy(out, j);
}


static const AngaraFuncDef ENCODING_EXPORTS[] = {
    {"base64_encode",    Angara_encoding_base64_encode,    "s->s", NULL},
    {"base64_decode",    Angara_encoding_base64_decode,    "s->s", NULL},
    {"base32_encode",    Angara_encoding_base32_encode,    "s->s", NULL},
    {"base32_decode",    Angara_encoding_base32_decode,    "s->s", NULL},
    {"base64url_encode", Angara_encoding_base64url_encode, "s->s", NULL},
    {"base64url_decode", Angara_encoding_base64url_decode, "s->s", NULL},
    {"hex_encode",       Angara_encoding_hex_encode,       "s->s", NULL},
    {"hex_decode",       Angara_encoding_hex_decode,       "s->s", NULL},
    {"url_encode",       Angara_encoding_url_encode,       "s->s", NULL},
    {"url_decode",       Angara_encoding_url_decode,       "s->s", NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(encoding) {
    ang_api = api;
    *def_count = (sizeof(ENCODING_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return ENCODING_EXPORTS;
}