/// Angara compression module — zstd / bzip2 / xz compress & decompress.
///
/// Strings in Angara are byte strings, so compressed output is returned as a
/// string (which may contain NUL bytes — use str_len to get the real length).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zstd.h>
#include <bzlib.h>
#include <lzma.h>
#include "Angara.h"

#define IS_STR(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)


AngaraObject Angara_compress_zstd_compress(int arg_count, AngaraObject* args) {
    /* compress(data:string, level:i64?) -> compressed:string */

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


/* ---- bzip2 ---- */

AngaraObject Angara_compress_bzip2_compress(int arg_count, AngaraObject* args) {
    const char* src = ang_api->as_cstr(args[0]);
    size_t src_len = ang_api->str_len(args[0]);

    int blockSize = 9;  /* 100k blocks, max compression */
    if (arg_count >= 2 && ang_is_i64(args[1])) {
        blockSize = (int)ang_as_i64(args[1]);
        if (blockSize < 1) blockSize = 1;
        if (blockSize > 9) blockSize = 9;
    }

    /* worst-case: 101% of input + 600 bytes */
    unsigned int dst_len = (unsigned int)(src_len + (src_len / 100) + 600);
    char* dst = (char*)malloc(dst_len);
    if (!dst) {
        ang_api->throw_error("compress.bzip2: out of memory.");
        return ang_nil();
    }

    int rc = BZ2_bzBuffToBuffCompress(dst, &dst_len,
                                      (char*)src, (unsigned int)src_len,
                                      blockSize, 0, 0);
    if (rc != BZ_OK) {
        free(dst);
        char buf[128];
        snprintf(buf, sizeof(buf), "compress.bzip2: error %d", rc);
        ang_api->throw_error(buf);
        return ang_nil();
    }

    return ang_api->string_no_copy(dst, dst_len);
}

AngaraObject Angara_compress_bzip2_decompress(int arg_count, AngaraObject* args) {
    const char* src = ang_api->as_cstr(args[0]);
    size_t src_len = ang_api->str_len(args[0]);

    /* guess decompressed size: typical 4-5× expansion, cap at 256 MiB */
    size_t dst_cap = src_len * 5;
    if (dst_cap < 4096) dst_cap = 4096;
    if (dst_cap > 256 * 1024 * 1024) dst_cap = 256 * 1024 * 1024;
    char* dst = (char*)malloc(dst_cap);
    if (!dst) {
        ang_api->throw_error("decompress.bzip2: out of memory.");
        return ang_nil();
    }

    unsigned int dst_len = (unsigned int)dst_cap;
    int rc = BZ2_bzBuffToBuffDecompress(dst, &dst_len,
                                        (char*)src, (unsigned int)src_len,
                                        0, 0);
    /* if output buffer was too small, retry with a larger one */
    while (rc == BZ_OUTBUFF_FULL) {
        free(dst);
        dst_cap *= 2;
        if (dst_cap > 256 * 1024 * 1024) {
            ang_api->throw_error("decompress.bzip2: output exceeds 256 MiB limit.");
            return ang_nil();
        }
        dst = (char*)malloc(dst_cap);
        if (!dst) {
            ang_api->throw_error("decompress.bzip2: out of memory.");
            return ang_nil();
        }
        dst_len = (unsigned int)dst_cap;
        rc = BZ2_bzBuffToBuffDecompress(dst, &dst_len,
                                        (char*)src, (unsigned int)src_len,
                                        0, 0);
    }
    if (rc != BZ_OK) {
        free(dst);
        char buf[128];
        snprintf(buf, sizeof(buf), "decompress.bzip2: error %d", rc);
        ang_api->throw_error(buf);
        return ang_nil();
    }

    return ang_api->string_no_copy(dst, dst_len);
}


/* ---- xz / lzma ---- */

AngaraObject Angara_compress_xz_compress(int arg_count, AngaraObject* args) {
    const uint8_t* src = (const uint8_t*)ang_api->as_cstr(args[0]);
    size_t src_len = ang_api->str_len(args[0]);

    uint32_t preset = 6;  /* LZMA_PRESET_DEFAULT */
    if (arg_count >= 2 && ang_is_i64(args[1])) {
        preset = (uint32_t)ang_as_i64(args[1]);
        if (preset > 9) preset = 9;
    }

    /* worst-case bound from lzma_stream_buffer_bound */
    size_t dst_cap = lzma_stream_buffer_bound(src_len);
    uint8_t* dst = (uint8_t*)malloc(dst_cap);
    if (!dst) {
        ang_api->throw_error("compress.xz: out of memory.");
        return ang_nil();
    }

    size_t out_pos = 0;
    lzma_ret rc = lzma_easy_buffer_encode(preset, LZMA_CHECK_CRC64, NULL,
                                          src, src_len, dst, &out_pos, dst_cap);
    if (rc != LZMA_OK) {
        free(dst);
        char buf[128];
        snprintf(buf, sizeof(buf), "compress.xz: error %d", (int)rc);
        ang_api->throw_error(buf);
        return ang_nil();
    }

    return ang_api->string_no_copy((char*)dst, out_pos);
}

AngaraObject Angara_compress_xz_decompress(int arg_count, AngaraObject* args) {
    const uint8_t* src = (const uint8_t*)ang_api->as_cstr(args[0]);
    size_t src_len = ang_api->str_len(args[0]);

    /* allocate output buffer; start with 4× input size, cap at 256 MiB */
    size_t dst_cap = src_len * 4;
    if (dst_cap < 4096) dst_cap = 4096;
    if (dst_cap > 256 * 1024 * 1024) dst_cap = 256 * 1024 * 1024;
    uint8_t* dst = (uint8_t*)malloc(dst_cap);
    if (!dst) {
        ang_api->throw_error("decompress.xz: out of memory.");
        return ang_nil();
    }

    size_t in_pos = 0, out_pos = 0;
    uint64_t memlimit = 128ULL * 1024 * 1024;  /* 128 MiB memory limit */
    uint32_t flags = 0;

    lzma_ret rc = lzma_stream_buffer_decode(&memlimit, flags, NULL,
                                            src, &in_pos, src_len,
                                            dst, &out_pos, dst_cap);
    /* retry with larger buffer if necessary */
    while (rc == LZMA_BUF_ERROR) {
        free(dst);
        dst_cap *= 2;
        if (dst_cap > 256 * 1024 * 1024) {
            ang_api->throw_error("decompress.xz: output exceeds 256 MiB limit.");
            return ang_nil();
        }
        dst = (uint8_t*)malloc(dst_cap);
        if (!dst) {
            ang_api->throw_error("decompress.xz: out of memory.");
            return ang_nil();
        }
        in_pos = 0; out_pos = 0;
        rc = lzma_stream_buffer_decode(&memlimit, flags, NULL,
                                       src, &in_pos, src_len,
                                       dst, &out_pos, dst_cap);
    }
    if (rc != LZMA_OK) {
        free(dst);
        char buf[128];
        snprintf(buf, sizeof(buf), "decompress.xz: error %d", (int)rc);
        ang_api->throw_error(buf);
        return ang_nil();
    }

    return ang_api->string_no_copy((char*)dst, out_pos);
}


static const AngaraFuncDef COMPRESS_EXPORTS[] = {
    {"zstd_compress",     Angara_compress_zstd_compress,     "si?->s", NULL},
    {"zstd_decompress",   Angara_compress_zstd_decompress,   "s->s",   NULL},
    {"zstd_max_level",    Angara_compress_zstd_max_level,    "->i",    NULL},
    {"bzip2_compress",    Angara_compress_bzip2_compress,    "si?->s", NULL},
    {"bzip2_decompress",  Angara_compress_bzip2_decompress,  "s->s",   NULL},
    {"xz_compress",       Angara_compress_xz_compress,       "si?->s", NULL},
    {"xz_decompress",     Angara_compress_xz_decompress,     "s->s",   NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(compress) {
    ang_api = api;
    *def_count = (sizeof(COMPRESS_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return COMPRESS_EXPORTS;
}
