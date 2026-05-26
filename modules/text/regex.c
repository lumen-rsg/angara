/// Angara regex module — POSIX extended regex match, search, replace, and split.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include "Angara.h"

#define IS_STR(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)

AngaraObject Angara_regex_match(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_STR(args[0]) || !IS_STR(args[1])) {
        ang_api->throw_error("regex.match(pattern, text) expects two string arguments.");
        return ang_nil();
    }
    const char* pattern = ang_api->as_cstr(args[0]);
    const char* text = ang_api->as_cstr(args[1]);

    regex_t regex;
    int ret = regcomp(&regex, pattern, REG_EXTENDED);
    if (ret != 0) {
        char errbuf[256];
        regerror(ret, &regex, errbuf, sizeof(errbuf));
        ang_api->throw_error(errbuf);
        return ang_nil();
    }

    int result = regexec(&regex, text, 0, NULL, 0);
    regfree(&regex);
    return ang_bool(result == 0);
}

AngaraObject Angara_regex_find(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_STR(args[0]) || !IS_STR(args[1])) {
        ang_api->throw_error("regex.find(pattern, text) expects two string arguments.");
        return ang_nil();
    }
    const char* pattern = ang_api->as_cstr(args[0]);
    const char* text = ang_api->as_cstr(args[1]);
    size_t text_len = ang_api->str_len(args[1]);

    regex_t regex;
    int ret = regcomp(&regex, pattern, REG_EXTENDED);
    if (ret != 0) {
        char errbuf[256];
        regerror(ret, &regex, errbuf, sizeof(errbuf));
        ang_api->throw_error(errbuf);
        return ang_nil();
    }

    AngaraObject result_list = ang_api->list_new();
    size_t nsub = regex.re_nsub + 1;
    regmatch_t* matches = (regmatch_t*)malloc(nsub * sizeof(regmatch_t));
    if (!matches) {
        regfree(&regex);
        ang_api->throw_error("regex.find: out of memory.");
        return ang_nil();
    }

    const char* search_start = text;
    int eflags = 0;

    while (1) {
        int exec_ret = regexec(&regex, search_start, nsub, matches, eflags);
        if (exec_ret != 0) break;

        AngaraObject rec = ang_api->record_new();

        if (matches[0].rm_so >= 0 && matches[0].rm_eo >= 0) {
            size_t match_len = (size_t)(matches[0].rm_eo - matches[0].rm_so);
            char* match_str = (char*)malloc(match_len + 1);
            memcpy(match_str, search_start + matches[0].rm_so, match_len);
            match_str[match_len] = '\0';
            ang_api->record_set(rec, "match", ang_api->string_no_copy(match_str, match_len));

            size_t abs_start = (size_t)(search_start - text) + (size_t)matches[0].rm_so;
            ang_api->record_set(rec, "start", ang_i64((int64_t)abs_start));
        }

        if (regex.re_nsub > 0) {
            AngaraObject groups = ang_api->list_new();
            for (size_t g = 1; g < nsub; g++) {
                if (matches[g].rm_so >= 0 && matches[g].rm_eo >= 0) {
                    size_t glen = (size_t)(matches[g].rm_eo - matches[g].rm_so);
                    char* gstr = (char*)malloc(glen + 1);
                    memcpy(gstr, search_start + matches[g].rm_so, glen);
                    gstr[glen] = '\0';
                    ang_api->list_push(groups, ang_api->string_no_copy(gstr, glen));
                } else {
                    ang_api->list_push(groups, ang_nil());
                }
            }
            ang_api->record_set(rec, "groups", groups);
            ang_api->decref(groups);
        }

        ang_api->list_push(result_list, rec);
        ang_api->decref(rec);

        size_t advance = (size_t)matches[0].rm_eo;
        if (advance == 0) advance = 1;
        search_start += advance;

        if ((size_t)(search_start - text) >= text_len) break;

        eflags = REG_NOTBOL;
    }

    free(matches);
    regfree(&regex);
    return result_list;
}

AngaraObject Angara_regex_replace(int arg_count, AngaraObject* args) {
    if (arg_count != 3 || !IS_STR(args[0]) || !IS_STR(args[1]) || !IS_STR(args[2])) {
        ang_api->throw_error("regex.replace(pattern, text, replacement) expects three string arguments.");
        return ang_nil();
    }
    const char* pattern = ang_api->as_cstr(args[0]);
    const char* text = ang_api->as_cstr(args[1]);
    const char* replacement = ang_api->as_cstr(args[2]);
    size_t text_len = ang_api->str_len(args[1]);
    size_t repl_len = ang_api->str_len(args[2]);

    regex_t regex;
    int ret = regcomp(&regex, pattern, REG_EXTENDED);
    if (ret != 0) {
        char errbuf[256];
        regerror(ret, &regex, errbuf, sizeof(errbuf));
        ang_api->throw_error(errbuf);
        return ang_nil();
    }

    size_t nsub = regex.re_nsub + 1;
    regmatch_t* matches = (regmatch_t*)malloc(nsub * sizeof(regmatch_t));
    if (!matches) {
        regfree(&regex);
        ang_api->throw_error("regex.replace: out of memory.");
        return ang_nil();
    }

    size_t buf_cap = text_len * 2 + 256;
    char* buf = (char*)malloc(buf_cap);
    if (!buf) {
        free(matches);
        regfree(&regex);
        ang_api->throw_error("regex.replace: out of memory.");
        return ang_nil();
    }
    size_t buf_len = 0;

    const char* search_start = text;
    int eflags = 0;

    while (1) {
        int exec_ret = regexec(&regex, search_start, nsub, matches, eflags);
        if (exec_ret != 0) {
            size_t remaining = text_len - (size_t)(search_start - text);
            if (buf_len + remaining >= buf_cap) {
                buf_cap = buf_len + remaining + 1;
                char* new_buf = (char*)realloc(buf, buf_cap);
                if (!new_buf) { free(buf); free(matches); regfree(&regex); return ang_nil(); }
                buf = new_buf;
            }
            memcpy(buf + buf_len, search_start, remaining);
            buf_len += remaining;
            break;
        }

        size_t before = (size_t)matches[0].rm_so;
        if (buf_len + before >= buf_cap) {
            buf_cap = buf_len + before + repl_len + 256;
            char* new_buf = (char*)realloc(buf, buf_cap);
            if (!new_buf) { free(buf); free(matches); regfree(&regex); return ang_nil(); }
            buf = new_buf;
        }
        memcpy(buf + buf_len, search_start, before);
        buf_len += before;

        for (size_t r = 0; r < repl_len; r++) {
            if (replacement[r] == '$' && r + 1 < repl_len && replacement[r + 1] >= '0' && replacement[r + 1] <= '9') {
                int group_num = replacement[r + 1] - '0';
                r++;
                if (group_num > 0 && (size_t)group_num < nsub && matches[group_num].rm_so >= 0) {
                    size_t glen = (size_t)(matches[group_num].rm_eo - matches[group_num].rm_so);
                    if (buf_len + glen >= buf_cap) {
                        buf_cap = buf_len + glen + 256;
                        char* new_buf = (char*)realloc(buf, buf_cap);
                        if (!new_buf) { free(buf); free(matches); regfree(&regex); return ang_nil(); }
                        buf = new_buf;
                    }
                    memcpy(buf + buf_len, search_start + matches[group_num].rm_so, glen);
                    buf_len += glen;
                }
            } else {
                if (buf_len + 1 >= buf_cap) {
                    buf_cap *= 2;
                    char* new_buf = (char*)realloc(buf, buf_cap);
                    if (!new_buf) { free(buf); free(matches); regfree(&regex); return ang_nil(); }
                    buf = new_buf;
                }
                buf[buf_len++] = replacement[r];
            }
        }

        size_t advance = (size_t)matches[0].rm_eo;
        if (advance == 0) advance = 1;
        search_start += advance;

        if ((size_t)(search_start - text) >= text_len) break;
        eflags = REG_NOTBOL;
    }

    buf[buf_len] = '\0';
    free(matches);
    regfree(&regex);
    return ang_api->string_no_copy(buf, buf_len);
}

AngaraObject Angara_regex_split(int arg_count, AngaraObject* args) {
    if (arg_count < 2 || !IS_STR(args[0]) || !IS_STR(args[1])) {
        ang_api->throw_error("regex.split(pattern, text, max_splits?) expects at least two string arguments.");
        return ang_nil();
    }
    const char* pattern = ang_api->as_cstr(args[0]);
    const char* text = ang_api->as_cstr(args[1]);
    size_t text_len = ang_api->str_len(args[1]);
    int64_t max_splits = -1;
    if (arg_count >= 3 && ang_is_i64(args[2])) {
        max_splits = ang_as_i64(args[2]);
    }

    regex_t regex;
    int ret = regcomp(&regex, pattern, REG_EXTENDED);
    if (ret != 0) {
        char errbuf[256];
        regerror(ret, &regex, errbuf, sizeof(errbuf));
        ang_api->throw_error(errbuf);
        return ang_nil();
    }

    AngaraObject result_list = ang_api->list_new();
    size_t nsub = regex.re_nsub + 1;
    regmatch_t* matches = (regmatch_t*)malloc(nsub * sizeof(regmatch_t));
    if (!matches) {
        regfree(&regex);
        ang_api->throw_error("regex.split: out of memory.");
        return ang_nil();
    }

    const char* search_start = text;
    int64_t split_count = 0;
    int eflags = 0;

    while (1) {
        if (max_splits >= 0 && split_count >= max_splits) {
            size_t remaining = text_len - (size_t)(search_start - text);
            if (remaining > 0) {
                ang_api->list_push(result_list, ang_api->string_len(search_start, remaining));
            } else {
                ang_api->list_push(result_list, ang_api->string(""));
            }
            break;
        }

        int exec_ret = regexec(&regex, search_start, nsub, matches, eflags);
        if (exec_ret != 0) {
            size_t remaining = text_len - (size_t)(search_start - text);
            if (remaining > 0) {
                ang_api->list_push(result_list, ang_api->string_len(search_start, remaining));
            } else if (split_count > 0) {
                ang_api->list_push(result_list, ang_api->string(""));
            }
            break;
        }

        size_t before = (size_t)matches[0].rm_so;
        ang_api->list_push(result_list, ang_api->string_len(search_start, before));

        size_t advance = (size_t)matches[0].rm_eo;
        if (advance == 0) advance = 1;
        search_start += advance;
        split_count++;

        if ((size_t)(search_start - text) >= text_len) {
            ang_api->list_push(result_list, ang_api->string(""));
            break;
        }
        eflags = REG_NOTBOL;
    }

    free(matches);
    regfree(&regex);
    return result_list;
}

AngaraObject Angara_regex_test(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_STR(args[0]) || !IS_STR(args[1])) {
        ang_api->throw_error("regex.test(pattern, text) expects two string arguments.");
        return ang_nil();
    }
    const char* pattern = ang_api->as_cstr(args[0]);
    const char* text = ang_api->as_cstr(args[1]);

    size_t plen = strlen(pattern);
    char* anchored = (char*)malloc(plen + 3);
    if (!anchored) { ang_api->throw_error("regex.test: out of memory."); return ang_nil(); }
    anchored[0] = '^';
    memcpy(anchored + 1, pattern, plen);
    anchored[plen + 1] = '$';
    anchored[plen + 2] = '\0';

    regex_t regex;
    int ret = regcomp(&regex, anchored, REG_EXTENDED);
    free(anchored);
    if (ret != 0) {
        char errbuf[256];
        regerror(ret, &regex, errbuf, sizeof(errbuf));
        ang_api->throw_error(errbuf);
        return ang_nil();
    }

    int result = regexec(&regex, text, 0, NULL, 0);
    regfree(&regex);
    return ang_bool(result == 0);
}

static const AngaraFuncDef REGEX_EXPORTS[] = {
    {"match",   Angara_regex_match,   "ss->b",    NULL},
    {"test",    Angara_regex_test,    "ss->b",    NULL},
    {"find",    Angara_regex_find,    "ss->l<{}>", NULL},
    {"replace", Angara_regex_replace, "sss->s",   NULL},
    {"split",   Angara_regex_split,   "ssi?->l<s>", NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(regex) {
    ang_api = api;
    *def_count = (sizeof(REGEX_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return REGEX_EXPORTS;
}
