/// Angara MessagePack module — encode / decode via msgpack binary format.
/// Zero external dependencies; implements the msgpack spec directly.
///
/// Spec reference: https://github.com/msgpack/msgpack/blob/master/spec.md
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Angara.h"

// =============================================================================
//  Helpers
// =============================================================================

#define IS_STR(v)  (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)
#define IS_REC(v)  (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_RECORD)
#define IS_LIST(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_LIST)

// =============================================================================
//  Dynamic byte buffer
// =============================================================================

typedef struct {
    uint8_t* data;
    size_t   cap;
    size_t   len;
} ByteBuf;

static void bb_init(ByteBuf* bb, size_t cap) {
    bb->cap  = cap;
    bb->len  = 0;
    bb->data = (uint8_t*)malloc(cap);
}

static void bb_grow(ByteBuf* bb, size_t needed) {
    while (bb->cap < needed) bb->cap *= 2;
    bb->data = (uint8_t*)realloc(bb->data, bb->cap);
}

static void bb_write(ByteBuf* bb, const uint8_t* src, size_t n) {
    size_t need = bb->len + n;
    if (need > bb->cap) bb_grow(bb, need);
    memcpy(bb->data + bb->len, src, n);
    bb->len += n;
}

static void bb_write_byte(ByteBuf* bb, uint8_t b) {
    size_t need = bb->len + 1;
    if (need > bb->cap) bb_grow(bb, need);
    bb->data[bb->len++] = b;
}

// =============================================================================
//  Big-endian integer helpers
// =============================================================================

static void bb_write_u16_be(ByteBuf* bb, uint16_t v) {
    bb_write_byte(bb, (uint8_t)((v >> 8) & 0xFF));
    bb_write_byte(bb, (uint8_t)(v & 0xFF));
}

static void bb_write_u32_be(ByteBuf* bb, uint32_t v) {
    bb_write_byte(bb, (uint8_t)((v >> 24) & 0xFF));
    bb_write_byte(bb, (uint8_t)((v >> 16) & 0xFF));
    bb_write_byte(bb, (uint8_t)((v >> 8)  & 0xFF));
    bb_write_byte(bb, (uint8_t)(v & 0xFF));
}

static void bb_write_u64_be(ByteBuf* bb, uint64_t v) {
    bb_write_u32_be(bb, (uint32_t)(v >> 32));
    bb_write_u32_be(bb, (uint32_t)(v & 0xFFFFFFFF));
}

static uint16_t read_u16_be(const uint8_t* data) {
    return (uint16_t)((data[0] << 8) | data[1]);
}

static uint32_t read_u32_be(const uint8_t* data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8)  |  (uint32_t)data[3];
}

static uint64_t read_u64_be(const uint8_t* data) {
    return ((uint64_t)read_u32_be(data) << 32) |
            (uint64_t)read_u32_be(data + 4);
}

// =============================================================================
//  Encode: any Angara value → msgpack bytes
// =============================================================================

/// Forward declaration for recursion.
static void msgpack_encode_value(AngaraObject obj, ByteBuf* bb);

/// Write a msgpack str header (fixstr, str8, str16, or str32).
static void msgpack_write_str_header(ByteBuf* bb, size_t len) {
    if (len <= 31) {
        bb_write_byte(bb, (uint8_t)(0xa0 | len));       // fixstr
    } else if (len <= 0xFF) {
        bb_write_byte(bb, 0xd9);                         // str 8
        bb_write_byte(bb, (uint8_t)len);
    } else if (len <= 0xFFFF) {
        bb_write_byte(bb, 0xda);                         // str 16
        bb_write_u16_be(bb, (uint16_t)len);
    } else {
        bb_write_byte(bb, 0xdb);                         // str 32
        bb_write_u32_be(bb, (uint32_t)len);
    }
}

/// Encode an integer using the smallest msgpack representation.
static void msgpack_encode_int(ByteBuf* bb, int64_t v) {
    if (v >= 0) {
        if (v <= 0x7F) {
            bb_write_byte(bb, (uint8_t)v);               // positive fixint
        } else if (v <= 0xFF) {
            bb_write_byte(bb, 0xcc);                     // uint 8
            bb_write_byte(bb, (uint8_t)v);
        } else if (v <= 0xFFFF) {
            bb_write_byte(bb, 0xcd);                     // uint 16
            bb_write_u16_be(bb, (uint16_t)v);
        } else if (v <= 0xFFFFFFFFLL) {
            bb_write_byte(bb, 0xce);                     // uint 32
            bb_write_u32_be(bb, (uint32_t)v);
        } else {
            bb_write_byte(bb, 0xcf);                     // uint 64
            bb_write_u64_be(bb, (uint64_t)v);
        }
    } else {
        if (v >= -32) {
            bb_write_byte(bb, (uint8_t)(v & 0xFF));      // negative fixint
        } else if (v >= -128) {
            bb_write_byte(bb, 0xd0);                     // int 8
            bb_write_byte(bb, (uint8_t)(v & 0xFF));
        } else if (v >= -32768) {
            bb_write_byte(bb, 0xd1);                     // int 16
            bb_write_u16_be(bb, (uint16_t)(v & 0xFFFF));
        } else if (v >= -2147483648LL) {
            bb_write_byte(bb, 0xd2);                     // int 32
            bb_write_u32_be(bb, (uint32_t)(v & 0xFFFFFFFF));
        } else {
            bb_write_byte(bb, 0xd3);                     // int 64
            bb_write_u64_be(bb, (uint64_t)v);
        }
    }
}

static void msgpack_encode_value(AngaraObject obj, ByteBuf* bb) {
    if (ang_is_nil(obj)) {
        bb_write_byte(bb, 0xc0);
        return;
    }

    if (ang_is_bool(obj)) {
        bb_write_byte(bb, ang_as_bool(obj) ? 0xc3 : 0xc2);
        return;
    }

    if (ang_is_i64(obj)) {
        msgpack_encode_int(bb, ang_as_i64(obj));
        return;
    }

    if (ang_is_f64(obj)) {
        bb_write_byte(bb, 0xcb);                          // float 64
        uint64_t bits;
        memcpy(&bits, &(double){ang_as_f64(obj)}, sizeof(uint64_t));
        bb_write_u64_be(bb, bits);
        return;
    }

    if (IS_STR(obj)) {
        const char* s   = ang_api->as_cstr(obj);
        size_t      slen = ang_api->str_len(obj);
        msgpack_write_str_header(bb, slen);
        bb_write(bb, (const uint8_t*)s, slen);
        return;
    }

    if (IS_LIST(obj)) {
        size_t llen = ang_api->list_len(obj);
        // Write array header.
        if (llen <= 15) {
            bb_write_byte(bb, (uint8_t)(0x90 | llen));   // fixarray
        } else if (llen <= 0xFFFF) {
            bb_write_byte(bb, 0xdc);                     // array 16
            bb_write_u16_be(bb, (uint16_t)llen);
        } else {
            bb_write_byte(bb, 0xdd);                     // array 32
            bb_write_u32_be(bb, (uint32_t)llen);
        }
        for (size_t i = 0; i < llen; i++) {
            AngaraObject elem = ang_api->list_get(obj, (int64_t)i);
            msgpack_encode_value(elem, bb);
            ang_api->decref(elem);
        }
        return;
    }

    if (IS_REC(obj)) {
        size_t rlen = ang_api->record_len(obj);
        // Write map header.
        if (rlen <= 15) {
            bb_write_byte(bb, (uint8_t)(0x80 | rlen));   // fixmap
        } else if (rlen <= 0xFFFF) {
            bb_write_byte(bb, 0xde);                     // map 16
            bb_write_u16_be(bb, (uint16_t)rlen);
        } else {
            bb_write_byte(bb, 0xdf);                     // map 32
            bb_write_u32_be(bb, (uint32_t)rlen);
        }
        for (size_t i = 0; i < rlen; i++) {
            const char*  key = ang_api->record_key_at(obj, i);
            AngaraObject val = ang_api->record_val_at(obj, i);
            // Key is always a string in Angara records.
            size_t klen = strlen(key);
            msgpack_write_str_header(bb, klen);
            bb_write(bb, (const uint8_t*)key, klen);
            msgpack_encode_value(val, bb);
            ang_api->decref(val);
        }
        return;
    }

    // Unknown type: convert to string.
    AngaraObject str = ang_api->to_string(obj);
    const char* s   = ang_api->as_cstr(str);
    size_t      slen = ang_api->str_len(str);
    msgpack_write_str_header(bb, slen);
    bb_write(bb, (const uint8_t*)s, slen);
    ang_api->decref(str);
}

AngaraObject Angara_msgpack_encode(int arg_count, AngaraObject args[]) {
    (void)arg_count;
    ByteBuf bb;
    bb_init(&bb, 4096);
    msgpack_encode_value(args[0], &bb);
    return ang_api->string_len((const char*)bb.data, bb.len);
}

// =============================================================================
//  Decode: msgpack bytes → Angara value
// =============================================================================

/// Forward declaration for recursion.
static AngaraObject msgpack_decode_one(const uint8_t* data, size_t len,
                                       size_t* consumed);

/// Decode a single msgpack value starting at data[0..len-1].
/// Sets *consumed to bytes read.  Returns the decoded Angara value.
/// On error, sets *consumed to 0.
static AngaraObject msgpack_decode_one(const uint8_t* data, size_t len,
                                       size_t* consumed) {
    if (len == 0) goto fail;
    uint8_t tag = data[0];

    // --- nil ---
    if (tag == 0xc0) {
        *consumed = 1;
        return ang_nil();
    }

    // --- bool ---
    if (tag == 0xc2) { *consumed = 1; return ang_bool(false); }
    if (tag == 0xc3) { *consumed = 1; return ang_bool(true);  }

    // --- positive fixint (0x00 - 0x7f) ---
    if (tag <= 0x7f) {
        *consumed = 1;
        return ang_i64((int64_t)tag);
    }

    // --- negative fixint (0xe0 - 0xff) ---
    if (tag >= 0xe0) {
        *consumed = 1;
        return ang_i64((int64_t)((int8_t)tag));  // sign-extend
    }

    // --- fixstr (0xa0 - 0xbf) ---
    if ((tag & 0xe0) == 0xa0) {
        size_t slen = tag & 0x1f;
        if (len < 1 + slen) goto fail;
        *consumed = 1 + slen;
        return ang_api->string_len((const char*)(data + 1), slen);
    }

    // --- fixarray (0x90 - 0x9f) ---
    if ((tag & 0xf0) == 0x90) {
        size_t alen = tag & 0x0f;
        AngaraObject list = ang_api->list_new();
        size_t pos = 1;
        for (size_t i = 0; i < alen; i++) {
            size_t elem_cons;
            AngaraObject elem = msgpack_decode_one(data + pos,
                                                    len - pos, &elem_cons);
            if (elem_cons == 0) { ang_api->decref(list); goto fail; }
            ang_api->list_push(list, elem);
            ang_api->decref(elem);
            pos += elem_cons;
        }
        *consumed = pos;
        return list;
    }

    // --- fixmap (0x80 - 0x8f) ---
    if ((tag & 0xf0) == 0x80) {
        size_t mlen = tag & 0x0f;
        AngaraObject record = ang_api->record_new();
        size_t pos = 1;
        for (size_t i = 0; i < mlen; i++) {
            // Key (must be a string in valid msgpack, though spec allows any)
            size_t key_cons;
            AngaraObject key_obj = msgpack_decode_one(data + pos,
                                                       len - pos, &key_cons);
            if (key_cons == 0 || !IS_STR(key_obj)) {
                ang_api->decref(key_obj);
                ang_api->decref(record);
                goto fail;
            }
            pos += key_cons;
            const char* key_cstr = ang_api->as_cstr(key_obj);

            // Value
            size_t val_cons;
            AngaraObject val = msgpack_decode_one(data + pos,
                                                   len - pos, &val_cons);
            ang_api->decref(key_obj);
            if (val_cons == 0) { ang_api->decref(record); goto fail; }
            pos += val_cons;
            ang_api->record_set(record, key_cstr, val);
            ang_api->decref(val);
        }
        *consumed = pos;
        return record;
    }

    // --- Multi-byte tags ---
    switch (tag) {

    // ---- Unsigned integers ----
    case 0xcc: // uint 8
        if (len < 2) goto fail;
        *consumed = 2;
        return ang_i64((int64_t)data[1]);

    case 0xcd: // uint 16
        if (len < 3) goto fail;
        *consumed = 3;
        return ang_i64((int64_t)read_u16_be(data + 1));

    case 0xce: // uint 32
        if (len < 5) goto fail;
        *consumed = 5;
        return ang_i64((int64_t)read_u32_be(data + 1));

    case 0xcf: // uint 64
        if (len < 9) goto fail;
        *consumed = 9;
        {
            uint64_t uv = read_u64_be(data + 1);
            // Values > INT64_MAX are clamped (msgpack allows them but i64
            // cannot hold them).
            if (uv > (uint64_t)INT64_MAX) uv = (uint64_t)INT64_MAX;
            return ang_i64((int64_t)uv);
        }

    // ---- Signed integers ----
    case 0xd0: // int 8
        if (len < 2) goto fail;
        *consumed = 2;
        return ang_i64((int64_t)((int8_t)data[1]));

    case 0xd1: // int 16
        if (len < 3) goto fail;
        *consumed = 3;
        return ang_i64((int64_t)((int16_t)read_u16_be(data + 1)));

    case 0xd2: // int 32
        if (len < 5) goto fail;
        *consumed = 5;
        return ang_i64((int64_t)((int32_t)read_u32_be(data + 1)));

    case 0xd3: // int 64
        if (len < 9) goto fail;
        *consumed = 9;
        return ang_i64((int64_t)read_u64_be(data + 1));

    // ---- Float ----
    case 0xca: // float 32
        if (len < 5) goto fail;
        *consumed = 5;
        {
            uint32_t bits = read_u32_be(data + 1);
            float f;
            memcpy(&f, &bits, sizeof(float));
            return ang_f64((double)f);
        }

    case 0xcb: // float 64
        if (len < 9) goto fail;
        *consumed = 9;
        {
            uint64_t bits = read_u64_be(data + 1);
            double d;
            memcpy(&d, &bits, sizeof(double));
            return ang_f64(d);
        }

    // ---- str ----
    case 0xd9: // str 8
        if (len < 2) goto fail;
        {
            size_t slen = data[1];
            if (len < 2 + slen) goto fail;
            *consumed = 2 + slen;
            return ang_api->string_len((const char*)(data + 2), slen);
        }

    case 0xda: // str 16
        if (len < 3) goto fail;
        {
            size_t slen = read_u16_be(data + 1);
            if (len < 3 + slen) goto fail;
            *consumed = 3 + slen;
            return ang_api->string_len((const char*)(data + 3), slen);
        }

    case 0xdb: // str 32
        if (len < 5) goto fail;
        {
            size_t slen = (size_t)read_u32_be(data + 1);
            if (len < 5 + slen) goto fail;
            *consumed = 5 + slen;
            return ang_api->string_len((const char*)(data + 5), slen);
        }

    // ---- bin ----
    case 0xc4: // bin 8
        if (len < 2) goto fail;
        {
            size_t blen = data[1];
            if (len < 2 + blen) goto fail;
            *consumed = 2 + blen;
            return ang_api->string_len((const char*)(data + 2), blen);
        }

    case 0xc5: // bin 16
        if (len < 3) goto fail;
        {
            size_t blen = read_u16_be(data + 1);
            if (len < 3 + blen) goto fail;
            *consumed = 3 + blen;
            return ang_api->string_len((const char*)(data + 3), blen);
        }

    case 0xc6: // bin 32
        if (len < 5) goto fail;
        {
            size_t blen = (size_t)read_u32_be(data + 1);
            if (len < 5 + blen) goto fail;
            *consumed = 5 + blen;
            return ang_api->string_len((const char*)(data + 5), blen);
        }

    // ---- array ----
    case 0xdc: // array 16
        if (len < 3) goto fail;
        {
            size_t alen = read_u16_be(data + 1);
            AngaraObject list = ang_api->list_new();
            size_t pos = 3;
            for (size_t i = 0; i < alen; i++) {
                size_t elem_cons;
                AngaraObject elem = msgpack_decode_one(data + pos,
                                                        len - pos, &elem_cons);
                if (elem_cons == 0) { ang_api->decref(list); goto fail; }
                ang_api->list_push(list, elem);
                ang_api->decref(elem);
                pos += elem_cons;
            }
            *consumed = pos;
            return list;
        }

    case 0xdd: // array 32
        if (len < 5) goto fail;
        {
            size_t alen = (size_t)read_u32_be(data + 1);
            AngaraObject list = ang_api->list_new();
            size_t pos = 5;
            for (size_t i = 0; i < alen; i++) {
                size_t elem_cons;
                AngaraObject elem = msgpack_decode_one(data + pos,
                                                        len - pos, &elem_cons);
                if (elem_cons == 0) { ang_api->decref(list); goto fail; }
                ang_api->list_push(list, elem);
                ang_api->decref(elem);
                pos += elem_cons;
            }
            *consumed = pos;
            return list;
        }

    // ---- map ----
    case 0xde: // map 16
        if (len < 3) goto fail;
        {
            size_t mlen = read_u16_be(data + 1);
            AngaraObject record = ang_api->record_new();
            size_t pos = 3;
            for (size_t i = 0; i < mlen; i++) {
                size_t key_cons;
                AngaraObject key_obj = msgpack_decode_one(data + pos,
                                                           len - pos,
                                                           &key_cons);
                if (key_cons == 0 || !IS_STR(key_obj)) {
                    ang_api->decref(key_obj);
                    ang_api->decref(record);
                    goto fail;
                }
                pos += key_cons;
                const char* key_cstr = ang_api->as_cstr(key_obj);

                size_t val_cons;
                AngaraObject val = msgpack_decode_one(data + pos,
                                                       len - pos, &val_cons);
                ang_api->decref(key_obj);
                if (val_cons == 0) { ang_api->decref(record); goto fail; }
                pos += val_cons;
                ang_api->record_set(record, key_cstr, val);
                ang_api->decref(val);
            }
            *consumed = pos;
            return record;
        }

    case 0xdf: // map 32
        if (len < 5) goto fail;
        {
            size_t mlen = (size_t)read_u32_be(data + 1);
            AngaraObject record = ang_api->record_new();
            size_t pos = 5;
            for (size_t i = 0; i < mlen; i++) {
                size_t key_cons;
                AngaraObject key_obj = msgpack_decode_one(data + pos,
                                                           len - pos,
                                                           &key_cons);
                if (key_cons == 0 || !IS_STR(key_obj)) {
                    ang_api->decref(key_obj);
                    ang_api->decref(record);
                    goto fail;
                }
                pos += key_cons;
                const char* key_cstr = ang_api->as_cstr(key_obj);

                size_t val_cons;
                AngaraObject val = msgpack_decode_one(data + pos,
                                                       len - pos, &val_cons);
                ang_api->decref(key_obj);
                if (val_cons == 0) { ang_api->decref(record); goto fail; }
                pos += val_cons;
                ang_api->record_set(record, key_cstr, val);
                ang_api->decref(val);
            }
            *consumed = pos;
            return record;
        }

    // ---- ext (skip gracefully — return as raw binary string) ----
    case 0xc7: // ext 8
        if (len < 3) goto fail;
        {
            size_t elen = data[1];
            if (len < 3 + elen) goto fail;
            *consumed = 3 + elen;
            return ang_api->string_len((const char*)(data + 2), elen + 1);
        }
    case 0xc8: // ext 16
        if (len < 4) goto fail;
        {
            size_t elen = read_u16_be(data + 1);
            if (len < 4 + elen) goto fail;
            *consumed = 4 + elen;
            return ang_api->string_len((const char*)(data + 2), elen + 2);
        }
    case 0xc9: // ext 32
        if (len < 6) goto fail;
        {
            size_t elen = (size_t)read_u32_be(data + 1);
            if (len < 6 + elen) goto fail;
            *consumed = 6 + elen;
            return ang_api->string_len((const char*)(data + 2), elen + 4);
        }
    case 0xd4: // fixext 1
        if (len < 3) goto fail;
        *consumed = 3;
        return ang_api->string_len((const char*)(data + 1), 2);
    case 0xd5: // fixext 2
        if (len < 4) goto fail;
        *consumed = 4;
        return ang_api->string_len((const char*)(data + 1), 3);
    case 0xd6: // fixext 4
        if (len < 6) goto fail;
        *consumed = 6;
        return ang_api->string_len((const char*)(data + 1), 5);
    case 0xd7: // fixext 8
        if (len < 10) goto fail;
        *consumed = 10;
        return ang_api->string_len((const char*)(data + 1), 9);
    case 0xd8: // fixext 16
        if (len < 18) goto fail;
        *consumed = 18;
        return ang_api->string_len((const char*)(data + 1), 17);

    default:
        // 0xc1 (reserved) or unrecognised tag
        goto fail;
    }

fail:
    *consumed = 0;
    return ang_nil();
}

AngaraObject Angara_msgpack_decode(int arg_count, AngaraObject args[]) {
    (void)arg_count;

    if (!IS_STR(args[0])) {
        ang_api->throw_error(
            "msgpack.decode: expected a binary string as argument.");
        return ang_nil();
    }

    const uint8_t* data = (const uint8_t*)ang_api->as_cstr(args[0]);
    size_t len = ang_api->str_len(args[0]);

    size_t consumed;
    AngaraObject result = msgpack_decode_one(data, len, &consumed);

    if (consumed == 0) {
        ang_api->throw_error(
            "msgpack.decode: invalid or truncated msgpack data.");
        return ang_nil();
    }

    // If there are trailing bytes, it's not a clean single-value decode,
    // but we still return the first value (like many msgpack impls).
    return result;
}

// =============================================================================
//  Module exports
// =============================================================================

static const AngaraFuncDef MSGPACK_EXPORTS[] = {
    {"encode", Angara_msgpack_encode, "a->s", NULL},
    {"decode", Angara_msgpack_decode, "s->a", NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(msgpack) {
    ang_api = api;
    *def_count = (sizeof(MSGPACK_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return MSGPACK_EXPORTS;
}
