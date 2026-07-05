#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "Angara.h"
#include "json_bridge.h"

#define IS_STR(v)  (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)
#define IS_REC(v)  (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_RECORD)


static const char b64url_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static size_t base64url_encode(const uint8_t* data, size_t len, char* out) {
    size_t i = 0, j = 0;
    while (i < len) {
        uint32_t a = i < len ? data[i++] : 0;
        uint32_t b = i < len ? data[i++] : 0;
        uint32_t c = i < len ? data[i++] : 0;
        uint32_t triple = (a << 16) | (b << 8) | c;

        out[j++] = b64url_table[(triple >> 18) & 0x3F];
        out[j++] = b64url_table[(triple >> 12) & 0x3F];
        out[j++] = b64url_table[(triple >> 6)  & 0x3F];
        out[j++] = b64url_table[triple & 0x3F];
    }
    int pad = (3 - (len % 3)) % 3;
    j -= pad;
    out[j] = '\0';
    return j;
}

static int b64url_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-' || c == '+') return 62;
    if (c == '_' || c == '/') return 63;
    return -1;
}

static ssize_t base64url_decode(const char* in, size_t len, uint8_t* out) {
    size_t padded_len = len;
    int pad = (4 - (len % 4)) % 4;
    char* padded = (char*)malloc(len + pad + 4);
    memcpy(padded, in, len);
    for (int i = 0; i < pad; i++) padded[len + i] = '=';
    padded[len + pad] = '\0';

    size_t j = 0;
    for (size_t i = 0; i < len + pad; i += 4) {
        int a = b64url_val(padded[i]);
        int b = (i + 1 < len + pad) ? b64url_val(padded[i + 1]) : 0;
        int c = (i + 2 < len + pad) ? b64url_val(padded[i + 2]) : 0;
        int d = (i + 3 < len + pad) ? b64url_val(padded[i + 3]) : 0;
        if (a < 0 || b < 0) { free(padded); return -1; }

        uint32_t triple = (a << 18) | (b << 12) | (c << 6) | d;
        out[j++] = (triple >> 16) & 0xFF;
        if (i + 2 < len) out[j++] = (triple >> 8) & 0xFF;
        if (i + 3 < len) out[j++] = triple & 0xFF;
    }
    free(padded);
    return (ssize_t)j;
}




static const uint32_t sha256_k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t buffer[64];
} SHA256_CTX;

static void sha256_init(SHA256_CTX* ctx) {
    ctx->state[0]=0x6a09e667; ctx->state[1]=0xbb67ae85;
    ctx->state[2]=0x3c6ef372; ctx->state[3]=0xa54ff53a;
    ctx->state[4]=0x510e527f; ctx->state[5]=0x9b05688c;
    ctx->state[6]=0x1f83d9ab; ctx->state[7]=0x5be0cd19;
    ctx->count = 0;
}

#define ROTR32(x,n) (((x)>>(n))|((x)<<(32-(n))))
#define CH(x,y,z) (((x)&(y))^(~(x)&(z)))
#define MAJ(x,y,z) (((x)&(y))^((x)&(z))^((y)&(z)))
#define EP0(x) (ROTR32(x,2)^ROTR32(x,13)^ROTR32(x,22))
#define EP1(x) (ROTR32(x,6)^ROTR32(x,11)^ROTR32(x,25))
#define SIG0(x) (ROTR32(x,7)^ROTR32(x,18)^((x)>>3))
#define SIG1(x) (ROTR32(x,17)^ROTR32(x,19)^((x)>>10))

static void sha256_transform(uint32_t state[8], const uint8_t block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)block[i*4]<<24)|((uint32_t)block[i*4+1]<<16)|
               ((uint32_t)block[i*4+2]<<8)|block[i*4+3];
    for (int i = 16; i < 64; i++)
        w[i] = SIG1(w[i-2]) + w[i-7] + SIG0(w[i-15]) + w[i-16];

    uint32_t a=state[0],b=state[1],c=state[2],d=state[3];
    uint32_t e=state[4],f=state[5],g=state[6],h=state[7];

    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + EP1(e) + CH(e,f,g) + sha256_k[i] + w[i];
        uint32_t t2 = EP0(a) + MAJ(a,b,c);
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d;
    state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
}

static void sha256_update(SHA256_CTX* ctx, const uint8_t* data, size_t len) {
    size_t idx = (size_t)(ctx->count % 64);
    ctx->count += len;
    for (size_t i = 0; i < len; i++) {
        ctx->buffer[idx++] = data[i];
        if (idx == 64) { sha256_transform(ctx->state, ctx->buffer); idx = 0; }
    }
}

static void sha256_final(SHA256_CTX* ctx, uint8_t hash[32]) {
    uint64_t bits = ctx->count * 8;
    uint8_t pad = 0x80;
    sha256_update(ctx, &pad, 1);
    pad = 0;
    while ((ctx->count % 64) != 56) sha256_update(ctx, &pad, 1);

    uint8_t len_be[8];
    for (int i = 7; i >= 0; i--) { len_be[i] = bits & 0xFF; bits >>= 8; }
    sha256_update(ctx, len_be, 8);

    for (int i = 0; i < 8; i++) {
        hash[i*4]   = (ctx->state[i] >> 24) & 0xFF;
        hash[i*4+1] = (ctx->state[i] >> 16) & 0xFF;
        hash[i*4+2] = (ctx->state[i] >> 8) & 0xFF;
        hash[i*4+3] = ctx->state[i] & 0xFF;
    }
}

static void hmac_sha256(const uint8_t* key, size_t key_len,
                         const uint8_t* data, size_t data_len,
                         uint8_t out[32]) {
    uint8_t k[64];
    memset(k, 0, 64);
    if (key_len > 64) {
        SHA256_CTX ctx;
        sha256_init(&ctx);
        sha256_update(&ctx, key, key_len);
        sha256_final(&ctx, k);
    } else {
        memcpy(k, key, key_len);
    }

    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; i++) {
        ipad[i] = k[i] ^ 0x36;
        opad[i] = k[i] ^ 0x5C;
    }

    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, ipad, 64);
    sha256_update(&ctx, data, data_len);
    uint8_t inner[32];
    sha256_final(&ctx, inner);

    sha256_init(&ctx);
    sha256_update(&ctx, opad, 64);
    sha256_update(&ctx, inner, 32);
    sha256_final(&ctx, out);
}


static AngaraObject json_to_angara(JsonHandle handle) {
    if (!handle) return ang_nil();
    if (json_bridge_is_object(handle)) {
        AngaraObject rec = ang_api->record_new();
        size_t sz = json_bridge_object_size(handle);
        for (size_t i = 0; i < sz; ++i) {
            const char* key = json_bridge_object_get_key_at(handle, i);
            JsonHandle val = json_bridge_object_get_value_at(handle, i);
            if (key && val) {
                AngaraObject av = json_to_angara(val);
                ang_api->record_set(rec, key, av);
                ang_api->decref(av);
            }
            if (key) free((void*)key);
        }
        return rec;
    }
    if (json_bridge_is_array(handle)) {
        AngaraObject list = ang_api->list_new();
        size_t sz = json_bridge_array_size(handle);
        for (size_t i = 0; i < sz; ++i) {
            JsonHandle elem = json_bridge_array_get_element(handle, i);
            AngaraObject av = json_to_angara(elem);
            ang_api->list_push(list, av);
            ang_api->decref(av);
        }
        return list;
    }
    if (json_bridge_is_string(handle)) {
        const char* s = json_bridge_get_string(handle);
        AngaraObject obj = ang_api->string(s ? s : "");
        if (s) free((void*)s);
        return obj;
    }
    if (json_bridge_is_number(handle))  return ang_f64(json_bridge_get_number(handle));
    if (json_bridge_is_boolean(handle)) return ang_bool(json_bridge_get_boolean(handle));
    if (json_bridge_is_null(handle))    return ang_nil();
    return ang_nil();
}

static JsonHandle angara_to_json(AngaraObject obj) {
    if (ang_is_nil(obj))   return json_bridge_new_null();
    if (ang_is_bool(obj))  return json_bridge_new_bool(ang_as_bool(obj));
    if (ang_is_i64(obj))   return json_bridge_new_number((double)ang_as_i64(obj));
    if (ang_is_f64(obj))   return json_bridge_new_number(ang_as_f64(obj));
    if (ang_is_obj(obj)) {
        int32_t otype = ang_api->obj_type(obj);
        if (otype == ANG_OBJ_STRING) return json_bridge_new_string(ang_api->as_cstr(obj));
        if (otype == ANG_OBJ_LIST) {
            JsonHandle arr = json_bridge_new_array();
            size_t len = ang_api->list_len(obj);
            for (size_t i = 0; i < len; ++i) {
                AngaraObject elem = ang_api->list_get(obj, (int64_t)i);
                JsonHandle jh = angara_to_json(elem);
                json_bridge_array_add(arr, jh);
                json_bridge_free(jh);
                ang_api->decref(elem);
            }
            return arr;
        }
        if (otype == ANG_OBJ_RECORD) {
            JsonHandle obj_h = json_bridge_new_object();
            size_t len = ang_api->record_len(obj);
            for (size_t i = 0; i < len; ++i) {
                const char* key = ang_api->record_key_at(obj, i);
                AngaraObject val = ang_api->record_val_at(obj, i);
                JsonHandle jh = angara_to_json(val);
                json_bridge_object_add(obj_h, key, jh);
                json_bridge_free(jh);
                ang_api->decref(val);
            }
            return obj_h;
        }
    }
    return json_bridge_new_null();
}


AngaraObject Angara_jwt_create(int arg_count, AngaraObject* args) {
    if (arg_count < 2 || !IS_REC(args[0]) || !IS_STR(args[1])) {
        ang_api->throw_error("create(payload, secret, algorithm?) expects a record and a string.");
        return ang_nil();
    }

    const char* alg = "HS256";
    if (arg_count >= 3 && IS_STR(args[2])) alg = ang_api->as_cstr(args[2]);

    JsonHandle header = json_bridge_new_object();
    if (strcmp(alg, "HS256") == 0) {
        json_bridge_object_add(header, "alg", json_bridge_new_string("HS256"));
    } else if (strcmp(alg, "HS384") == 0 || strcmp(alg, "HS512") == 0) {
        ang_api->throw_error("jwt.create: only HS256 is currently supported.");
        json_bridge_free(header);
        return ang_nil();
    } else {
        ang_api->throw_error("jwt.create: unknown algorithm. Use HS256.");
        json_bridge_free(header);
        return ang_nil();
    }
    json_bridge_object_add(header, "typ", json_bridge_new_string("JWT"));

    const char* header_json = json_bridge_stringify(header);
    json_bridge_free(header);
    size_t header_json_len = strlen(header_json);
    char header_b64[256];
    size_t header_b64_len = base64url_encode((const uint8_t*)header_json, header_json_len, header_b64);
    json_bridge_free_string((char*)header_json);

    JsonHandle payload_json = angara_to_json(args[0]);
    const char* payload_str = json_bridge_stringify(payload_json);
    json_bridge_free(payload_json);
    size_t payload_str_len = strlen(payload_str);
    char* payload_b64 = (char*)malloc(payload_str_len * 2 + 4);
    size_t payload_b64_len = base64url_encode((const uint8_t*)payload_str, payload_str_len, payload_b64);
    json_bridge_free_string((char*)payload_str);

    size_t sig_input_len = header_b64_len + 1 + payload_b64_len;
    char* sig_input = (char*)malloc(sig_input_len + 1);
    snprintf(sig_input, sig_input_len + 1, "%s.%s", header_b64, payload_b64);

    const char* secret = ang_api->as_cstr(args[1]);
    size_t secret_len = ang_api->str_len(args[1]);

    uint8_t signature[32];
    hmac_sha256((const uint8_t*)secret, secret_len,
                (const uint8_t*)sig_input, sig_input_len, signature);

    char sig_b64[64];
    size_t sig_b64_len = base64url_encode(signature, 32, sig_b64);

    size_t token_len = header_b64_len + 1 + payload_b64_len + 1 + sig_b64_len;
    char* token = (char*)malloc(token_len + 1);
    snprintf(token, token_len + 1, "%s.%s.%s", header_b64, payload_b64, sig_b64);

    free(sig_input);
    free(payload_b64);
    return ang_api->string_no_copy(token, token_len);
}

AngaraObject Angara_jwt_verify(int arg_count, AngaraObject* args) {
    if (arg_count < 2 || !IS_STR(args[0]) || !IS_STR(args[1])) {
        ang_api->throw_error("verify(token, secret) expects two strings.");
        return ang_nil();
    }

    const char* token = ang_api->as_cstr(args[0]);
    size_t token_len = ang_api->str_len(args[0]);
    const char* secret = ang_api->as_cstr(args[1]);
    size_t secret_len = ang_api->str_len(args[1]);

    const char* dot1 = memchr(token, '.', token_len);
    if (!dot1) return ang_nil();
    size_t header_len = dot1 - token;
    const char* dot2 = memchr(dot1 + 1, '.', token_len - header_len - 1);
    if (!dot2) return ang_nil();
    size_t payload_len = dot2 - dot1 - 1;
    size_t sig_len = token_len - (dot2 + 1 - token);

    /* --- decode and validate the alg header (alg-confusion fix) --- */
    {
        uint8_t* header_bytes = (uint8_t*)malloc(header_len + 4);
        ssize_t hd = base64url_decode(token, header_len, header_bytes);
        int alg_ok = 0;
        if (hd > 0) {
            header_bytes[hd] = '\0';
            char* error_msg = NULL;
            JsonHandle header_json = json_bridge_parse((const char*)header_bytes, &error_msg);
            if (header_json && json_bridge_is_object(header_json)) {
                size_t sz = json_bridge_object_size(header_json);
                for (size_t i = 0; i < sz; i++) {
                    const char* key = json_bridge_object_get_key_at(header_json, i);
                    if (key && strcmp(key, "alg") == 0) {
                        JsonHandle alg_val = json_bridge_object_get_value_at(header_json, i);
                        if (alg_val && json_bridge_is_string(alg_val)) {
                            const char* alg_str = json_bridge_get_string(alg_val);
                            if (alg_str && strcmp(alg_str, "HS256") == 0) alg_ok = 1;
                            if (alg_str) free((void*)alg_str);
                        }
                    }
                    if (key) free((void*)key);
                }
                json_bridge_free(header_json);
            }
            if (error_msg) free(error_msg);
        }
        free(header_bytes);
        if (!alg_ok) return ang_nil();  /* reject unknown/missing/unsupported alg */
    }

    size_t sig_input_len = header_len + 1 + payload_len;
    char* sig_input = (char*)malloc(sig_input_len + 1);
    memcpy(sig_input, token, sig_input_len);
    sig_input[sig_input_len] = '\0';

    uint8_t expected_sig[32];
    hmac_sha256((const uint8_t*)secret, secret_len,
                (const uint8_t*)sig_input, sig_input_len, expected_sig);
    free(sig_input);

    /* --- decode signature into a heap buffer (stack-overflow fix) --- */
    /* max base64url-encoded length for a 64-byte hash is 86 chars;
       reject anything larger as obviously malicious */
    if (sig_len > 86) return ang_nil();
    size_t sig_buf_size = (sig_len * 3) / 4 + 4;
    uint8_t* provided_sig = (uint8_t*)malloc(sig_buf_size);
    ssize_t decoded_sig_len = base64url_decode(dot2 + 1, sig_len, provided_sig);
    if (decoded_sig_len != 32) {
        free(provided_sig);
        return ang_nil();
    }

    int diff = 0;
    for (int i = 0; i < 32; i++) diff |= provided_sig[i] ^ expected_sig[i];
    free(provided_sig);
    if (diff != 0) return ang_nil();

    uint8_t* payload_bytes = (uint8_t*)malloc(payload_len + 4);
    ssize_t decoded = base64url_decode(dot1 + 1, payload_len, payload_bytes);
    if (decoded < 0) { free(payload_bytes); return ang_nil(); }
    payload_bytes[decoded] = '\0';

    char* error_msg = NULL;
    JsonHandle payload_json = json_bridge_parse((const char*)payload_bytes, &error_msg);
    free(payload_bytes);

    if (!payload_json) {
        if (error_msg) free(error_msg);
        return ang_nil();
    }

    AngaraObject payload = json_to_angara(payload_json);
    json_bridge_free(payload_json);

    AngaraObject exp = ang_api->record_get(payload, "exp");
    if (ang_is_i64(exp)) {
        time_t now = time(NULL);
        if (ang_as_i64(exp) < (int64_t)now) {
            ang_api->decref(exp);
            ang_api->decref(payload);
            return ang_nil();
        }
    }
    ang_api->decref(exp);

    return payload;
}

AngaraObject Angara_jwt_decode(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !IS_STR(args[0])) {
        ang_api->throw_error("decode(token) expects a string.");
        return ang_nil();
    }

    const char* token = ang_api->as_cstr(args[0]);
    size_t token_len = ang_api->str_len(args[0]);

    const char* dot1 = memchr(token, '.', token_len);
    if (!dot1) return ang_nil();
    size_t header_len = dot1 - token;
    const char* dot2 = memchr(dot1 + 1, '.', token_len - header_len - 1);
    if (!dot2) return ang_nil();
    size_t payload_len = dot2 - dot1 - 1;

    AngaraObject result = ang_api->record_new();

    uint8_t* header_bytes = (uint8_t*)malloc(header_len + 4);
    ssize_t hd = base64url_decode(token, header_len, header_bytes);
    if (hd >= 0) {
        header_bytes[hd] = '\0';
        char* error_msg = NULL;
        JsonHandle header_json = json_bridge_parse((const char*)header_bytes, &error_msg);
        if (header_json) {
            AngaraObject header = json_to_angara(header_json);
            ang_api->record_set(result, "header", header);
            ang_api->decref(header);
            json_bridge_free(header_json);
        }
        if (error_msg) free(error_msg);
    }
    free(header_bytes);

    uint8_t* payload_bytes = (uint8_t*)malloc(payload_len + 4);
    ssize_t pd = base64url_decode(dot1 + 1, payload_len, payload_bytes);
    if (pd >= 0) {
        payload_bytes[pd] = '\0';
        char* error_msg = NULL;
        JsonHandle payload_json = json_bridge_parse((const char*)payload_bytes, &error_msg);
        if (payload_json) {
            AngaraObject payload = json_to_angara(payload_json);
            ang_api->record_set(result, "payload", payload);
            ang_api->decref(payload);
            json_bridge_free(payload_json);
        }
        if (error_msg) free(error_msg);
    }
    free(payload_bytes);

    size_t sig_len = token_len - (dot2 + 1 - token);
    ang_api->record_set(result, "signature", ang_api->string_len(dot2 + 1, sig_len));

    return result;
}


static const AngaraFuncDef JWT_EXPORTS[] = {
    {"create", Angara_jwt_create, "{}ss?->s", NULL},
    {"verify", Angara_jwt_verify, "ss->{}?",  NULL},
    {"decode", Angara_jwt_decode, "s->{}",    NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(jwt) {
    ang_api = api;
    *def_count = (sizeof(JWT_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return JWT_EXPORTS;
}