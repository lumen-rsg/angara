/// Angara compression module — zstd compress / decompress.
///
/// Strings in Angara are byte strings, so compressed output is returned as a
/// string (which may contain NUL bytes — use str_len to get the real length).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zstd.h>
#include "Angara.h"

#define IS_STR(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)


AngaraObject Angara_compress_zstd_compress(int arg_count, AngaraObject* args) {
    /* compress(data:string, level:i64?) -> compressed:string */
    if (arg_count < 1 || !IS_STR(args[0])) {
        ang_api->throw_error("compress.zstd(data, level?) expects a string.");
        return ang_nil();
    }

    const char* src = ang_api->as_cstr(args[0]);
    size_t src_len = ang_api->str_len(args[0]);

    int level = 3; /* zstd default */
    if (arg_count >= 2 && ang_is_i64(args[1])) {
        level = (int)ang_as_i64(args[1]);
        if (level < 1) level = 1;
        if (level > ZSTD_maxCLevel()) level = ZSTD_maxCLevel();
    }

    size_t dst_cap = ZSTD_compressBound(src_len);
    char* dst = (char*)malloc(dst_cap);
    if (!dst) {
        ang_api->throw_error("compress.zstd: out of memory.");
        return ang_nil();
    }

    size_t compressed = ZSTD_compress(dst, dst_cap, src, src_len, level);
    if (ZSTD_isError(compressed)) {
        free(dst);
        char buf[256];
        snprintf(buf, sizeof(buf), "compress.zstd: %s", ZSTD_getErrorName(compressed));
        ang_api->throw_error(buf);
        return ang_nil();
    }

    return ang_api->string_no_copy(dst, compressed);
}


AngaraObject Angara_compress_zstd_decompress(int arg_count, AngaraObject* args) {
    /* decompress(data:string) -> decompressed:string */
    if (arg_count < 1 || !IS_STR(args[0])) {
        ang_api->throw_error("decompress.zstd(data) expects a string.");
        return ang_nil();
    }

    const char* src = ang_api->as_cstr(args[0]);
    size_t src_len = ang_api->str_len(args[0]);

    /* get the decompressed size from the frame header */
    unsigned long long content_size = ZSTD_getFrameContentSize(src, src_len);
    if (content_size == ZSTD_CONTENTSIZE_ERROR) {
        ang_api->throw_error("decompress.zstd: not a valid zstd frame.");
        return ang_nil();
    }
    if (content_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        /* unknown size — use a streaming approach with a guess */
        content_size = src_len * 4;  /* reasonable upper bound */
    }

    size_t dst_cap = (size_t)content_size;
    char* dst = (char*)malloc(dst_cap + 1);
    if (!dst) {
        ang_api->throw_error("decompress.zstd: out of memory.");
        return ang_nil();
    }

    size_t decompressed = ZSTD_decompress(dst, dst_cap, src, src_len);
    if (ZSTD_isError(decompressed)) {
        /* maybe the content-size guess was too low; retry with a larger buffer */
        if (content_size != ZSTD_CONTENTSIZE_UNKNOWN) {
            free(dst);
            char buf[256];
            snprintf(buf, sizeof(buf), "decompress.zstd: %s", ZSTD_getErrorName(decompressed));
            ang_api->throw_error(buf);
            return ang_nil();
        }
        /* streaming fallback: use ZSTD_decompress with known src */
        size_t retry_cap = src_len * 32;
        char* retry = (char*)malloc(retry_cap);
        if (!retry) { free(dst); ang_api->throw_error("decompress.zstd: out of memory."); return ang_nil(); }
        decompressed = ZSTD_decompress(retry, retry_cap, src, src_len);
        if (ZSTD_isError(decompressed)) {
            free(retry);
            char buf[256];
            snprintf(buf, sizeof(buf), "decompress.zstd: %s", ZSTD_getErrorName(decompressed));
            ang_api->throw_error(buf);
            return ang_nil();
        }
        free(dst);
        return ang_api->string_no_copy(retry, decompressed);
    }

    return ang_api->string_no_copy(dst, decompressed);
}


AngaraObject Angara_compress_zstd_max_level(int arg_count, AngaraObject* args) {
    (void)arg_count; (void)args;
    return ang_i64(ZSTD_maxCLevel());
}


static const AngaraFuncDef COMPRESS_EXPORTS[] = {
    {"zstd_compress",   Angara_compress_zstd_compress,   "si?->s", NULL},
    {"zstd_decompress", Angara_compress_zstd_decompress, "s->s",   NULL},
    {"zstd_max_level",  Angara_compress_zstd_max_level,  "->i",    NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(compress) {
    ang_api = api;
    *def_count = (sizeof(COMPRESS_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return COMPRESS_EXPORTS;
}
