/// Angara Protobuf module — wire-format encode / decode.
/// Zero external dependencies; implements the protobuf binary wire format directly.
///
/// Wire format reference: https://protobuf.dev/programming-guides/encoding/
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

// Wire types
#define PB_WT_VARINT          0
#define PB_WT_FIXED64         1
#define PB_WT_LENGTH_DELIM    2
#define PB_WT_START_GROUP     3  // deprecated
#define PB_WT_END_GROUP       4  // deprecated
#define PB_WT_FIXED32         5

// =============================================================================
//  Dynamic buffer (for building output)
// =============================================================================

typedef struct {
    uint8_t* data;
    size_t   cap;
    size_t   len;
} ByteBuf;

static void bb_init(ByteBuf* bb, size_t initial_cap) {
    bb->cap  = initial_cap;
    bb->len  = 0;
    bb->data = (uint8_t*)malloc(initial_cap);
}

static void bb_grow(ByteBuf* bb, size_t needed) {
    while (bb->cap < needed) bb->cap *= 2;
    bb->data = (uint8_t*)realloc(bb->data, bb->cap);
}

static void bb_write(ByteBuf* bb, const uint8_t* src, size_t n) {
    size_t needed = bb->len + n;
    if (needed > bb->cap) bb_grow(bb, needed);
    memcpy(bb->data + bb->len, src, n);
    bb->len += n;
}

static void bb_write_byte(ByteBuf* bb, uint8_t b) {
    size_t needed = bb->len + 1;
    if (needed > bb->cap) bb_grow(bb, needed);
    bb->data[bb->len++] = b;
}

// =============================================================================
//  Varint encode / decode
// =============================================================================

/// Write an unsigned 64-bit varint to the buffer.
static void pb_write_varint(ByteBuf* bb, uint64_t value) {
    while (value > 0x7F) {
        bb_write_byte(bb, (uint8_t)((value & 0x7F) | 0x80));
        value >>= 7;
    }
    bb_write_byte(bb, (uint8_t)(value & 0x7F));
}

/// Write a signed 64-bit integer as a varint (using zigzag encoding for negative
/// values, matching protobuf's sint32/sint64 wire format).
static void pb_write_signed_varint(ByteBuf* bb, int64_t value) {
    // Zigzag encode: (n << 1) ^ (n >> 63)
    uint64_t zigzag = (uint64_t)((value << 1) ^ (value >> 63));
    pb_write_varint(bb, zigzag);
}

/// Read an unsigned varint from a byte buffer.
/// Returns the number of bytes consumed, or 0 on error.
/// Sets *value on success.
static size_t pb_read_varint(const uint8_t* data, size_t len, uint64_t* value) {
    *value = 0;
    int shift = 0;
    for (size_t i = 0; i < len && i < 10; i++) {
        uint8_t byte = data[i];
        *value |= (uint64_t)(byte & 0x7F) << shift;
        if (!(byte & 0x80)) return i + 1;  // done
        shift += 7;
    }
    // Overflow or truncated — treat as error
    return 0;
}

/// Read a signed varint (zigzag-decoded).
static size_t pb_read_signed_varint(const uint8_t* data, size_t len, int64_t* value) {
    uint64_t zigzag = 0;
    size_t consumed = pb_read_varint(data, len, &zigzag);
    if (consumed == 0) return 0;
    // Zigzag decode: (n >> 1) ^ -(n & 1)
    *value = (int64_t)((zigzag >> 1) ^ (~(zigzag & 1) + 1));
    return consumed;
}

// =============================================================================
//  Fixed-width encode / decode (little-endian)
// =============================================================================

static void pb_write_fixed64(ByteBuf* bb, uint64_t value) {
    for (int i = 0; i < 8; i++) {
        bb_write_byte(bb, (uint8_t)(value & 0xFF));
        value >>= 8;
    }
}

static int pb_read_fixed64(const uint8_t* data, size_t len, uint64_t* value) {
    if (len < 8) return 0;
    *value = 0;
    for (int i = 7; i >= 0; i--) {
        *value = (*value << 8) | data[i];
    }
    return 1;
}

static int pb_read_fixed32(const uint8_t* data, size_t len, uint32_t* value) {
    if (len < 4) return 0;
    *value = 0;
    for (int i = 3; i >= 0; i--) {
        *value = (*value << 8) | data[i];
    }
    return 1;
}

// =============================================================================
//  Tag encoding
// =============================================================================

/// Write a protobuf field tag: (field_number << 3) | wire_type
static void pb_write_tag(ByteBuf* bb, uint64_t field_number, int wire_type) {
    pb_write_varint(bb, (field_number << 3) | (uint64_t)wire_type);
}

/// Read a protobuf field tag. Returns consumed bytes (0 on error).
/// Sets *field_number and *wire_type on success.
static size_t pb_read_tag(const uint8_t* data, size_t len,
                          uint64_t* field_number, int* wire_type) {
    uint64_t tag = 0;
    size_t consumed = pb_read_varint(data, len, &tag);
    if (consumed == 0) return 0;
    *wire_type    = (int)(tag & 0x07);
    *field_number = tag >> 3;
    if (*field_number == 0) return 0;  // field 0 is illegal
    return consumed;
}

// =============================================================================
//  Forward declarations for recursion
// =============================================================================

static void encode_value(AngaraObject obj, uint64_t field_number, ByteBuf* bb);
static int  try_decode_message(const uint8_t* data, size_t len, AngaraObject* result);

// =============================================================================
//  protobuf.encode(record) -> string
// =============================================================================

/// Encode a single Angara value at the given field number.
static void encode_value(AngaraObject obj, uint64_t field_number, ByteBuf* bb) {
    if (ang_is_nil(obj)) {
        // nil — skip (do not encode)
        return;
    }

    if (ang_is_bool(obj)) {
        pb_write_tag(bb, field_number, PB_WT_VARINT);
        // Use zigzag so that decode is consistent: all varints are zigzag-decoded.
        pb_write_signed_varint(bb, ang_as_bool(obj) ? 1 : 0);
        return;
    }

    if (ang_is_i64(obj)) {
        pb_write_tag(bb, field_number, PB_WT_VARINT);
        pb_write_signed_varint(bb, ang_as_i64(obj));
        return;
    }

    if (ang_is_f64(obj)) {
        pb_write_tag(bb, field_number, PB_WT_FIXED64);
        uint64_t bits;
        memcpy(&bits, &(double){ang_as_f64(obj)}, sizeof(uint64_t));
        pb_write_fixed64(bb, bits);
        return;
    }

    if (IS_STR(obj)) {
        const char* s   = ang_api->as_cstr(obj);
        size_t      slen = ang_api->str_len(obj);
        pb_write_tag(bb, field_number, PB_WT_LENGTH_DELIM);
        pb_write_varint(bb, (uint64_t)slen);
        bb_write(bb, (const uint8_t*)s, slen);
        return;
    }

    if (IS_REC(obj)) {
        // Encode nested message into a temporary buffer, then write as
        // length-delimited.
        ByteBuf nested;
        bb_init(&nested, 256);
        size_t rlen = ang_api->record_len(obj);
        for (size_t i = 0; i < rlen; i++) {
            const char* key = ang_api->record_key_at(obj, i);
            AngaraObject val = ang_api->record_val_at(obj, i);

            // Parse the key as a field number
            char* endp;
            long long fn = strtoll(key, &endp, 10);
            if (endp != key && *endp == '\0' && fn > 0 &&
                (unsigned long long)fn <= 0x1FFFFFFF) {
                encode_value(val, (uint64_t)fn, &nested);
            }
            // If key is not a valid positive integer, skip
            ang_api->decref(val);
        }

        pb_write_tag(bb, field_number, PB_WT_LENGTH_DELIM);
        pb_write_varint(bb, (uint64_t)nested.len);
        bb_write(bb, nested.data, nested.len);
        free(nested.data);
        return;
    }

    if (IS_LIST(obj)) {
        // Encode each element as a separate field entry (non-packed repeated).
        size_t llen = ang_api->list_len(obj);
        for (size_t i = 0; i < llen; i++) {
            AngaraObject elem = ang_api->list_get(obj, (int64_t)i);
            encode_value(elem, field_number, bb);
            ang_api->decref(elem);
        }
        return;
    }

    // Unknown type: convert to string and encode as length-delimited.
    AngaraObject str = ang_api->to_string(obj);
    const char* s   = ang_api->as_cstr(str);
    size_t      slen = ang_api->str_len(str);
    pb_write_tag(bb, field_number, PB_WT_LENGTH_DELIM);
    pb_write_varint(bb, (uint64_t)slen);
    bb_write(bb, (const uint8_t*)s, slen);
    ang_api->decref(str);
}

AngaraObject Angara_protobuf_encode(int arg_count, AngaraObject args[]) {
    (void)arg_count;
    AngaraObject input = args[0];

    if (!IS_REC(input)) {
        ang_api->throw_error(
            "protobuf.encode: expected a record as argument.");
        return ang_nil();
    }

    ByteBuf bb;
    bb_init(&bb, 4096);

    size_t rlen = ang_api->record_len(input);
    for (size_t i = 0; i < rlen; i++) {
        const char* key = ang_api->record_key_at(input, i);
        AngaraObject val = ang_api->record_val_at(input, i);

        // Parse key as field number
        char* endp;
        long long fn = strtoll(key, &endp, 10);
        if (endp != key && *endp == '\0' && fn > 0 &&
            (unsigned long long)fn <= 0x1FFFFFFF) {
            encode_value(val, (uint64_t)fn, &bb);
        }
        // Invalid keys (non-numeric, zero, out of range) are silently skipped.

        ang_api->decref(val);
    }

    return ang_api->string_len((const char*)bb.data, bb.len);
}

// =============================================================================
//  protobuf.decode(bytes) -> record
// =============================================================================

/// Try to decode a byte range as a protobuf message.
/// Returns 1 on success (sets *result to a record), 0 if the data is not a
/// valid protobuf message.
static int try_decode_message(const uint8_t* data, size_t len,
                              AngaraObject* result) {
    AngaraObject record = ang_api->record_new();
    size_t pos = 0;
    int has_fields = 0;

    while (pos < len) {
        uint64_t field_number;
        int      wire_type;
        size_t consumed = pb_read_tag(data + pos, len - pos,
                                      &field_number, &wire_type);
        if (consumed == 0) {
            // Invalid tag — not a valid protobuf message.
            ang_api->decref(record);
            return 0;
        }
        has_fields = 1;
        pos += consumed;

        // Read the value based on wire type.
        AngaraObject value = ang_nil();
        int valid = 1;

        switch (wire_type) {
        case PB_WT_VARINT: {
            int64_t v;
            size_t c = pb_read_signed_varint(data + pos, len - pos, &v);
            if (c == 0) valid = 0;
            else { pos += c; value = ang_i64(v); }
            break;
        }
        case PB_WT_FIXED64: {
            uint64_t bits;
            if (!pb_read_fixed64(data + pos, len - pos, &bits))
                valid = 0;
            else {
                pos += 8;
                double d;
                memcpy(&d, &bits, sizeof(double));
                value = ang_f64(d);
            }
            break;
        }
        case PB_WT_LENGTH_DELIM: {
            uint64_t slen;
            size_t c = pb_read_varint(data + pos, len - pos, &slen);
            if (c == 0 || pos + c + slen > len) {
                valid = 0;
            } else {
                pos += c;
                // Try to decode as a nested message first.
                AngaraObject nested;
                if (slen > 0 &&
                    try_decode_message(data + pos, (size_t)slen, &nested)) {
                    value = nested;
                } else {
                    // Treat as a raw string (binary-safe).
                    value = ang_api->string_len(
                        (const char*)(data + pos), (size_t)slen);
                }
                pos += (size_t)slen;
            }
            break;
        }
        case PB_WT_FIXED32: {
            uint32_t bits;
            if (!pb_read_fixed32(data + pos, len - pos, &bits))
                valid = 0;
            else {
                pos += 4;
                float f;
                memcpy(&f, &bits, sizeof(float));
                value = ang_f64((double)f);
            }
            break;
        }
        case PB_WT_START_GROUP:
        case PB_WT_END_GROUP:
            // Deprecated — skip the entire group mechanism by treating as
            // invalid (we don't track group nesting).
            ang_api->decref(record);
            return 0;
        default:
            // Unknown wire type — invalid.
            ang_api->decref(record);
            return 0;
        }

        if (!valid) {
            ang_api->decref(record);
            return 0;
        }

        // Insert into the record, collecting repeated fields into lists.
        char key[32];
        snprintf(key, sizeof(key), "%llu",
                 (unsigned long long)field_number);

        AngaraObject existing = ang_api->record_get(record, key);
        if (ang_is_nil(existing)) {
            // First occurrence of this field number.
            ang_api->record_set(record, key, value);
        } else if (IS_LIST(existing)) {
            // Already a list — append.
            ang_api->list_push(existing, value);
        } else {
            // Convert to a list.
            AngaraObject list = ang_api->list_new();
            ang_api->list_push(list, existing);
            ang_api->list_push(list, value);
            ang_api->record_set(record, key, list);
            // No need to decref 'existing' — record_set transfers ownership
        }

        ang_api->decref(value);
        ang_api->decref(existing);
    }

    if (!has_fields) {
        // Empty message — still valid (empty record).
    }

    *result = record;
    return 1;
}

AngaraObject Angara_protobuf_decode(int arg_count, AngaraObject args[]) {
    (void)arg_count;

    if (!IS_STR(args[0])) {
        ang_api->throw_error(
            "protobuf.decode: expected a binary string as argument.");
        return ang_nil();
    }

    const uint8_t* data = (const uint8_t*)ang_api->as_cstr(args[0]);
    size_t len = ang_api->str_len(args[0]);

    AngaraObject result;
    if (!try_decode_message(data, len, &result)) {
        ang_api->throw_error(
            "protobuf.decode: invalid protobuf wire-format data.");
        return ang_nil();
    }

    return result;
}

// =============================================================================
//  Module exports
// =============================================================================

static const AngaraFuncDef PROTOBUF_EXPORTS[] = {
    {"encode", Angara_protobuf_encode, "{}->s", NULL},
    {"decode", Angara_protobuf_decode, "s->{}", NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(protobuf) {
    ang_api = api;
    *def_count = (sizeof(PROTOBUF_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return PROTOBUF_EXPORTS;
}
