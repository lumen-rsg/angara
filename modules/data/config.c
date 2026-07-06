/// Angara config module — INI, TOML, dotenv, and key=value file parsing.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "Angara.h"

#define IS_STR(v)  (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)
#define IS_REC(v)  (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_RECORD)

static char* trim(char* s) {
    while (isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    char* end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}

static char* trim_dup(const char* s) {
    while (isspace((unsigned char)*s)) s++;
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) len--;
    char* r = (char*)malloc(len + 1);
    memcpy(r, s, len);
    r[len] = '\0';
    return r;
}

static AngaraObject parse_toml_value(const char* val_str) {
    char* v = trim_dup(val_str);
    size_t vlen = strlen(v);

    if (vlen == 0) { free(v); return ang_api->string(""); }

    if (strcmp(v, "true") == 0) { free(v); return ang_bool(true); }
    if (strcmp(v, "false") == 0) { free(v); return ang_bool(false); }

    if ((v[0] == '-' && vlen > 1 && isdigit((unsigned char)v[1])) || isdigit((unsigned char)v[0])) {
        char* end;
        if (vlen > 2 && v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) {
            long long val = strtoll(v, &end, 16);
            if (*end == '\0') { free(v); return ang_i64((int64_t)val); }
        }
        if (vlen > 2 && v[0] == '0' && (v[1] == 'o' || v[1] == 'O')) {
            long long val = strtoll(v + 2, &end, 8);
            if (*end == '\0') { free(v); return ang_i64((int64_t)val); }
        }
        if (vlen > 2 && v[0] == '0' && (v[1] == 'b' || v[1] == 'B')) {
            long long val = strtoll(v + 2, &end, 2);
            if (*end == '\0') { free(v); return ang_i64((int64_t)val); }
        }
        if (strchr(v, '.') || strchr(v, 'e') || strchr(v, 'E')) {
            double dval = strtod(v, &end);
            if (*end == '\0') { free(v); return ang_f64(dval); }
        }
        long long val = strtoll(v, &end, 10);
        if (*end == '\0') { free(v); return ang_i64((int64_t)val); }
    }

    if ((v[0] == '"' && vlen >= 2 && v[vlen - 1] == '"') ||
        (v[0] == '\'' && vlen >= 2 && v[vlen - 1] == '\'')) {
        char* s = (char*)malloc(vlen - 1);
        memcpy(s, v + 1, vlen - 2);
        s[vlen - 2] = '\0';
        free(v);
        if (s[0] != '\0') {
            char* dst = s;
            char* src = s;
            while (*src) {
                if (*src == '\\' && *(src + 1)) {
                    src++;
                    switch (*src) {
                        case 'n': *dst++ = '\n'; break;
                        case 't': *dst++ = '\t'; break;
                        case 'r': *dst++ = '\r'; break;
                        case '\\': *dst++ = '\\'; break;
                        case '"': *dst++ = '"'; break;
                        case '\'': *dst++ = '\''; break;
                        default: *dst++ = '\\'; *dst++ = *src; break;
                    }
                } else {
                    *dst++ = *src;
                }
                src++;
            }
            *dst = '\0';
        }
        return ang_api->string_no_copy(s, strlen(s));
    }

    if (v[0] == '[' && v[vlen - 1] == ']') {
        AngaraObject list = ang_api->list_new();
        char* inner = (char*)malloc(vlen - 1);
        memcpy(inner, v + 1, vlen - 2);
        inner[vlen - 2] = '\0';

        char* tok = inner;
        while (*tok) {
            while (*tok && isspace((unsigned char)*tok)) tok++;
            if (!*tok) break;

            char* end = tok;
            int depth = 0;
            while (*end && !(*end == ',' && depth == 0)) {
                if (*end == '[' || *end == '{' || *end == '"' || *end == '\'') depth++;
                else if (*end == ']' || *end == '}' || *end == '"' || *end == '\'') depth--;
                end++;
            }
            char saved = *end;
            *end = '\0';

            AngaraObject elem = parse_toml_value(tok);
            ang_api->list_push(list, elem);
            ang_api->decref(elem);

            *end = saved;
            tok = end;
            if (*tok == ',') tok++;
        }
        free(inner);
        free(v);
        return list;
    }

    AngaraObject result = ang_api->string(v);
    free(v);
    return result;
}

AngaraObject Angara_config_parse_ini(int arg_count, AngaraObject* args) {

    if (arg_count < 1) { ang_api->throw_error("config.parse_ini: expected 1 argument"); return ang_nil(); }
    const char* text = ang_api->as_cstr(args[0]);
    size_t text_len = ang_api->str_len(args[0]);

    AngaraObject root = ang_api->record_new();
    AngaraObject current_section = root;
    ang_api->incref(root);

    char* buf = (char*)malloc(text_len + 1);
    memcpy(buf, text, text_len);
    buf[text_len] = '\0';

    char* line = strtok(buf, "\n");
    while (line) {
        char* trimmed = trim(line);
        size_t llen = strlen(trimmed);

        if (llen == 0 || trimmed[0] == '#' || trimmed[0] == ';') {
            line = strtok(NULL, "\n");
            continue;
        }

        if (trimmed[0] == '[' && llen >= 2 && trimmed[llen - 1] == ']') {
            trimmed[llen - 1] = '\0';
            char* section_name = trim(trimmed + 1);
            if (strlen(section_name) > 0) {
                AngaraObject sec = ang_api->record_new();
                ang_api->record_set(root, section_name, sec);
                ang_api->decref(current_section);
                current_section = sec;
            }
            line = strtok(NULL, "\n");
            continue;
        }

        char* eq = strchr(trimmed, '=');
        if (eq) {
            *eq = '\0';
            char* key = trim_dup(trimmed);
            char* val = trim_dup(eq + 1);
            size_t vlen = strlen(val);
            if (vlen >= 2 && ((val[0] == '"' && val[vlen-1] == '"') ||
                              (val[0] == '\'' && val[vlen-1] == '\''))) {
                memmove(val, val + 1, vlen - 2);
                val[vlen - 2] = '\0';
            }
            ang_api->record_set(current_section, key, ang_api->string(val));
            free(key);
            free(val);
        }

        line = strtok(NULL, "\n");
    }

    free(buf);
    ang_api->decref(current_section);
    return root;
}

AngaraObject Angara_config_stringify_ini(int arg_count, AngaraObject* args) {

    if (arg_count < 1) { ang_api->throw_error("config.stringify_ini: expected 1 argument"); return ang_nil(); }
    size_t cap = 4096;
    size_t len = 0;
    char* buf = (char*)malloc(cap);

    size_t rlen = ang_api->record_len(args[0]);
    for (size_t i = 0; i < rlen; i++) {
        const char* key = ang_api->record_key_at(args[0], i);
        AngaraObject val = ang_api->record_val_at(args[0], i);

        if (ang_is_obj(val) && ang_api->obj_type(val) == ANG_OBJ_RECORD) {
            size_t needed = strlen(key) + 4 + len + 2;
            while (needed >= cap) { cap *= 2; buf = (char*)realloc(buf, cap); }
            len += snprintf(buf + len, cap - len, "\n[%s]\n", key);

            size_t slen = ang_api->record_len(val);
            for (size_t j = 0; j < slen; j++) {
                const char* skey = ang_api->record_key_at(val, j);
                AngaraObject sval = ang_api->record_val_at(val, j);
                const char* sval_str = IS_STR(sval) ? ang_api->as_cstr(sval) : "";
                size_t inner = strlen(skey) + strlen(sval_str) + 4;
                while (len + inner >= cap) { cap *= 2; buf = (char*)realloc(buf, cap); }
                len += snprintf(buf + len, cap - len, "%s=%s\n", skey, sval_str);
                ang_api->decref(sval);
            }
        } else {
            const char* val_str = IS_STR(val) ? ang_api->as_cstr(val) : "";
            size_t needed = strlen(key) + strlen(val_str) + 4;
            while (len + needed >= cap) { cap *= 2; buf = (char*)realloc(buf, cap); }
            len += snprintf(buf + len, cap - len, "%s=%s\n", key, val_str);
        }
        ang_api->decref(val);
    }

    return ang_api->string_no_copy(buf, len);
}

AngaraObject Angara_config_parse_toml(int arg_count, AngaraObject* args) {

    if (arg_count < 1) { ang_api->throw_error("config.parse_toml: expected 1 argument"); return ang_nil(); }
    const char* text = ang_api->as_cstr(args[0]);
    size_t text_len = ang_api->str_len(args[0]);

    AngaraObject root = ang_api->record_new();
    AngaraObject current_section = root;
    ang_api->incref(root);

    char* buf = (char*)malloc(text_len + 1);
    memcpy(buf, text, text_len);
    buf[text_len] = '\0';

    char* saveptr;
    char* line = strtok_r(buf, "\n", &saveptr);
    while (line) {
        char* trimmed = trim(line);
        size_t llen = strlen(trimmed);

        if (llen == 0 || trimmed[0] == '#') {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        if (llen >= 4 && trimmed[0] == '[' && trimmed[1] == '[' && trimmed[llen-2] == ']' && trimmed[llen-1] == ']') {
            trimmed[llen - 2] = '\0';
            char* sec_name = trim(trimmed + 2);
            AngaraObject existing = ang_api->record_get(root, sec_name);
            if (ang_is_nil(existing)) {
                AngaraObject list = ang_api->list_new();
                AngaraObject sec = ang_api->record_new();
                ang_api->list_push(list, sec);
                ang_api->record_set(root, sec_name, list);
                ang_api->decref(current_section);
                current_section = sec;
                ang_api->decref(sec);
                ang_api->decref(existing);
            } else if (ang_is_obj(existing) && ang_api->obj_type(existing) == ANG_OBJ_LIST) {
                AngaraObject sec = ang_api->record_new();
                ang_api->list_push(existing, sec);
                ang_api->decref(current_section);
                current_section = sec;
                ang_api->decref(sec);
            }
            ang_api->decref(existing);
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        if (trimmed[0] == '[' && llen >= 2 && trimmed[llen - 1] == ']') {
            trimmed[llen - 1] = '\0';
            char* sec_name = trim(trimmed + 1);
            AngaraObject target = root;
            char* dot = sec_name;
            while (dot) {
                char* next_dot = strchr(dot, '.');
                if (next_dot) *next_dot = '\0';
                char* part = trim(dot);
                AngaraObject sub = ang_api->record_get(target, part);
                if (ang_is_nil(sub)) {
                    sub = ang_api->record_new();
                    ang_api->record_set(target, part, sub);
                    ang_api->decref(sub);
                }
                if (next_dot) {
                    target = sub;
                    dot = next_dot + 1;
                } else {
                    ang_api->decref(current_section);
                    current_section = sub;
                    ang_api->decref(sub);
                    dot = NULL;
                }
            }
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        char* eq = strchr(trimmed, '=');
        if (eq) {
            *eq = '\0';
            char* key = trim_dup(trimmed);
            char* val_str = trim_dup(eq + 1);

            if (val_str[0] != '"' && val_str[0] != '\'') {
                char* hash = strchr(val_str, '#');
                if (hash) {
                    if (hash > val_str && isspace((unsigned char)*(hash - 1))) {
                        *hash = '\0';
                        char* retrimmed = trim_dup(val_str);
                        free(val_str);
                        val_str = retrimmed;
                    }
                }
            }

            AngaraObject val = parse_toml_value(val_str);
            char* dot = key;
            AngaraObject target = current_section;
            while (dot) {
                char* next_dot = strchr(dot, '.');
                if (next_dot) {
                    *next_dot = '\0';
                    char* part = trim(dot);
                    AngaraObject sub = ang_api->record_get(target, part);
                    if (ang_is_nil(sub)) {
                        sub = ang_api->record_new();
                        ang_api->record_set(target, part, sub);
                    }
                    ang_api->decref(target);
                    target = sub;
                    ang_api->decref(sub);
                    dot = next_dot + 1;
                } else {
                    char* part = trim(dot);
                    ang_api->record_set(target, part, val);
                    ang_api->decref(val);
                    dot = NULL;
                }
            }

            free(key);
            free(val_str);
        }

        line = strtok_r(NULL, "\n", &saveptr);
    }

    free(buf);
    ang_api->decref(current_section);
    return root;
}

AngaraObject Angara_config_parse_dotenv(int arg_count, AngaraObject* args) {

    if (arg_count < 1) { ang_api->throw_error("config.parse_dotenv: expected 1 argument"); return ang_nil(); }
    const char* text = ang_api->as_cstr(args[0]);
    size_t text_len = ang_api->str_len(args[0]);

    AngaraObject rec = ang_api->record_new();

    char* buf = (char*)malloc(text_len + 1);
    memcpy(buf, text, text_len);
    buf[text_len] = '\0';

    char* saveptr;
    char* line = strtok_r(buf, "\n", &saveptr);
    while (line) {
        char* trimmed = trim(line);
        size_t llen = strlen(trimmed);

        if (llen == 0 || trimmed[0] == '#') {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        if (strncmp(trimmed, "export ", 7) == 0) {
            trimmed += 7;
            trimmed = trim(trimmed);
            llen = strlen(trimmed);
        }

        char* eq = strchr(trimmed, '=');
        if (eq) {
            *eq = '\0';
            char* key = trim_dup(trimmed);
            char* val = trim_dup(eq + 1);
            size_t vlen = strlen(val);

            if (vlen >= 2 && ((val[0] == '"' && val[vlen-1] == '"') ||
                              (val[0] == '\'' && val[vlen-1] == '\''))) {
                memmove(val, val + 1, vlen - 2);
                val[vlen - 2] = '\0';
            }

            ang_api->record_set(rec, key, ang_api->string(val));
            free(key);
            free(val);
        }

        line = strtok_r(NULL, "\n", &saveptr);
    }

    free(buf);
    return rec;
}

AngaraObject Angara_config_stringify_dotenv(int arg_count, AngaraObject* args) {

    if (arg_count < 1) { ang_api->throw_error("config.stringify_dotenv: expected 1 argument"); return ang_nil(); }
    size_t cap = 4096;
    size_t len = 0;
    char* buf = (char*)malloc(cap);

    size_t rlen = ang_api->record_len(args[0]);
    for (size_t i = 0; i < rlen; i++) {
        const char* key = ang_api->record_key_at(args[0], i);
        AngaraObject val = ang_api->record_val_at(args[0], i);
        const char* val_str = IS_STR(val) ? ang_api->as_cstr(val) : "";

        int needs_quote = 0;
        for (const char* p = val_str; *p; p++) {
            if (isspace((unsigned char)*p) || *p == '"' || *p == '\'' || *p == '$') {
                needs_quote = 1;
                break;
            }
        }

        size_t needed = strlen(key) + strlen(val_str) + 8;
        while (len + needed >= cap) { cap *= 2; buf = (char*)realloc(buf, cap); }

        if (needs_quote) {
            len += snprintf(buf + len, cap - len, "%s=\"%s\"\n", key, val_str);
        } else {
            len += snprintf(buf + len, cap - len, "%s=%s\n", key, val_str);
        }
        ang_api->decref(val);
    }

    return ang_api->string_no_copy(buf, len);
}

static const AngaraFuncDef CONFIG_EXPORTS[] = {
    {"parse_ini",       Angara_config_parse_ini,       "s->{}",  NULL},
    {"stringify_ini",   Angara_config_stringify_ini,    "{}->s",  NULL},
    {"parse_toml",      Angara_config_parse_toml,       "s->{}",  NULL},
    {"parse_dotenv",    Angara_config_parse_dotenv,      "s->{}",  NULL},
    {"stringify_dotenv",Angara_config_stringify_dotenv,  "{}->s",  NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(config) {
    ang_api = api;
    *def_count = (sizeof(CONFIG_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return CONFIG_EXPORTS;
}