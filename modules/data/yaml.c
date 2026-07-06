/// Angara YAML module — parse and stringify via libyaml.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "Angara.h"
#include "yaml.h"

// =============================================================================
//  Helpers
// =============================================================================

#define IS_STR(v)  (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)
#define IS_REC(v)  (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_RECORD)
#define IS_LIST(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_LIST)

/// Grow a dynamic buffer. Returns new pointer (may be realloc'd).
static void buf_grow(char** buf, size_t* cap, size_t needed) {
    while (*cap < needed) *cap *= 2;
    *buf = (char*)realloc(*buf, *cap);
}

/// Check if a string needs quoting in YAML.
/// Returns 1 if the scalar must be quoted.
static int yaml_needs_quotes(const char* s, size_t len) {
    if (len == 0) return 1; // empty string needs quotes

    // Leading/trailing whitespace
    if (isspace((unsigned char)s[0]) || isspace((unsigned char)s[len - 1]))
        return 1;

    // Check for YAML special characters
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (c == ':' || c == '#' || c == '{' || c == '}' ||
            c == '[' || c == ']' || c == ',' || c == '&' ||
            c == '*' || c == '?' || c == '|' || c == '>' ||
            c == '!' || c == '%' || c == '@' || c == '`' ||
            c == '\'' || c == '"' || c == '\n' || c == '\r' ||
            c == '\t')
            return 1;

        // Strings that look like booleans / null / numbers need quoting
        // to avoid ambiguity. We handle this by checking if the string
        // could be misinterpreted.
    }

    // Check for YAML-reserved words / patterns
    if (strcmp(s, "true") == 0 || strcmp(s, "false") == 0 ||
        strcmp(s, "yes") == 0  || strcmp(s, "no") == 0 ||
        strcmp(s, "on") == 0   || strcmp(s, "off") == 0 ||
        strcmp(s, "null") == 0 || strcmp(s, "NULL") == 0 ||
        strcmp(s, "Null") == 0 || strcmp(s, "~") == 0 ||
        strcmp(s, "y") == 0    || strcmp(s, "Y") == 0 ||
        strcmp(s, "n") == 0    || strcmp(s, "N") == 0)
        return 1;

    // Check if it could be interpreted as a number
    int dots = 0;
    int digits = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '.') dots++;
        else if (isdigit((unsigned char)s[i]) || s[i] == '-' || s[i] == '+') digits++;
        else break;
    }
    if (digits > 0 && (dots <= 1)) {
        // Could look like a number; check more carefully
        char* endp;
        strtod(s, &endp);
        if (*endp == '\0') return 1; // it IS a number
        // Also check for hex, octal, etc.
        if (len >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) return 1;
    }

    // Starts with special indicator characters
    if (s[0] == '-' || s[0] == ':' || s[0] == '?' ||
        s[0] == '&' || s[0] == '*' || s[0] == '!' ||
        s[0] == '%' || s[0] == '@' || s[0] == '`' ||
        s[0] == '|' || s[0] == '>' || s[0] == '\'' ||
        s[0] == '"')
        return 1;

    return 0;
}

// =============================================================================
//  Anchor table — for resolving YAML anchors & aliases
// =============================================================================

#define MAX_ANCHORS 64

typedef struct {
    char*         name;
    AngaraObject  value;
} AnchorEntry;

static AnchorEntry anchor_table[MAX_ANCHORS];
static int        anchor_count = 0;

static void anchors_clear(void) {
    for (int i = 0; i < anchor_count; i++) {
        free(anchor_table[i].name);
        ang_api->decref(anchor_table[i].value);
    }
    anchor_count = 0;
}

static void anchor_set(const char* name, AngaraObject value) {
    if (anchor_count >= MAX_ANCHORS) return;
    anchor_table[anchor_count].name  = strdup(name);
    anchor_table[anchor_count].value = value;
    ang_api->incref(value);
    anchor_count++;
}

static AngaraObject anchor_get(const char* name) {
    for (int i = 0; i < anchor_count; i++) {
        if (strcmp(anchor_table[i].name, name) == 0)
            return anchor_table[i].value;
    }
    return ang_nil();
}

// =============================================================================
//  Parse stack — for building the Angara object tree from YAML events
// =============================================================================

#define MAX_STACK 128

typedef enum {
    STACK_SEQUENCE = 0,
    STACK_MAPPING  = 1,
} StackType;

typedef struct {
    AngaraObject container;    // The list or record being built
    StackType    type;         // Sequence or mapping?
    char*        pending_key;  // For mappings: key waiting for its value
} StackFrame;

static StackFrame parse_stack[MAX_STACK];
static int        parse_stack_depth = 0;

static void push_stack(AngaraObject container, StackType type) {
    if (parse_stack_depth >= MAX_STACK) {
        ang_api->throw_error("YAML parse error: nesting depth exceeded.");
        return;
    }
    parse_stack[parse_stack_depth].container   = container;
    parse_stack[parse_stack_depth].type        = type;
    parse_stack[parse_stack_depth].pending_key = NULL;
    parse_stack_depth++;
}

static StackFrame* top_stack(void) {
    if (parse_stack_depth == 0) return NULL;
    return &parse_stack[parse_stack_depth - 1];
}

static void pop_stack(void) {
    if (parse_stack_depth > 0) {
        parse_stack_depth--;
        StackFrame* frame = &parse_stack[parse_stack_depth];
        free(frame->pending_key);
        frame->pending_key = NULL;
    }
}

/// Add a value to the currently-open container.
static void add_to_parent(AngaraObject value) {
    if (parse_stack_depth == 0) return;

    StackFrame* parent = top_stack();
    if (parent->type == STACK_SEQUENCE) {
        // Append to list
        ang_api->list_push(parent->container, value);
    } else {
        // Mapping: value is either the key or the value
        if (parent->pending_key == NULL) {
            // This is the key — must be a string
            if (IS_STR(value)) {
                parent->pending_key = strdup(ang_api->as_cstr(value));
            } else {
                // Non-string key: convert to string representation
                AngaraObject str = ang_api->to_string(value);
                parent->pending_key = strdup(ang_api->as_cstr(str));
                ang_api->decref(str);
            }
            // Don't incref the key — it's already handled
        } else {
            // This is the value — insert key=value pair
            ang_api->record_set(parent->container, parent->pending_key, value);
            free(parent->pending_key);
            parent->pending_key = NULL;
        }
    }
}

// =============================================================================
//  Scalar conversion — YAML scalar string → AngaraObject
// =============================================================================

/// Parse a YAML scalar value string into the appropriate Angara type.
/// Handles null, booleans, integers, floats, and plain strings.
static AngaraObject yaml_scalar_to_angara(const char* value, size_t len,
                                          yaml_scalar_style_t style) {
    if (style == YAML_PLAIN_SCALAR_STYLE) {
        // Plain scalars can be null, bool, int, float

        // Null
        if (len == 0 || strcmp(value, "null") == 0 ||
            strcmp(value, "NULL") == 0 || strcmp(value, "Null") == 0 ||
            strcmp(value, "~") == 0)
            return ang_nil();

        // Booleans
        if (strcmp(value, "true") == 0  || strcmp(value, "True") == 0  ||
            strcmp(value, "TRUE") == 0  || strcmp(value, "yes") == 0  ||
            strcmp(value, "Yes") == 0   || strcmp(value, "YES") == 0  ||
            strcmp(value, "on") == 0    || strcmp(value, "On") == 0   ||
            strcmp(value, "ON") == 0)
            return ang_bool(true);

        if (strcmp(value, "false") == 0 || strcmp(value, "False") == 0 ||
            strcmp(value, "FALSE") == 0 || strcmp(value, "no") == 0   ||
            strcmp(value, "No") == 0    || strcmp(value, "NO") == 0   ||
            strcmp(value, "off") == 0   || strcmp(value, "Off") == 0  ||
            strcmp(value, "OFF") == 0)
            return ang_bool(false);

        // Integer detection: plain digits or 0x/0o/0b prefixes
        if (len > 0) {
            const char* p = value;
            if (*p == '-' || *p == '+') p++;

            if (*p == '0' && len > 2) {
                if (*(p+1) == 'x' || *(p+1) == 'X') {
                    // Hex
                    char* end;
                    long long v = strtoll(p, &end, 16);
                    if (*end == '\0') return ang_i64((int64_t)v);
                } else if (*(p+1) == 'o' || *(p+1) == 'O') {
                    // Octal
                    char* end;
                    long long v = strtoll(p + 2, &end, 8);
                    if (*end == '\0') return ang_i64((int64_t)v);
                }
            }

            // Decimal integer or float
            int has_dot = 0, has_exp = 0;
            const char* q = p;
            while (*q) {
                if (*q == '.') has_dot++;
                else if (*q == 'e' || *q == 'E') has_exp++;
                else if (!isdigit((unsigned char)*q) && *q != '-' && *q != '+')
                    break;
                q++;
            }

            if (*q == '\0') {
                if (has_dot || has_exp) {
                    // Float
                    char* end;
                    double d = strtod(value, &end);
                    if (*end == '\0') return ang_f64(d);
                } else {
                    // Integer
                    char* end;
                    long long v = strtoll(value, &end, 10);
                    if (*end == '\0') return ang_i64((int64_t)v);
                }
            }
        }

        // Fall through: treat as string
        return ang_api->string(value);
    }

    // Quoted or literal/block scalars are always strings
    return ang_api->string(value);
}

// =============================================================================
//  yaml.parse(string) -> any
// =============================================================================

AngaraObject Angara_yaml_parse(int arg_count, AngaraObject args[]) {
    (void)arg_count;

    const char* input = ang_api->as_cstr(args[0]);

    yaml_parser_t parser;
    if (!yaml_parser_initialize(&parser)) {
        ang_api->throw_error("YAML parse error: failed to initialize parser.");
        return ang_nil();
    }

    yaml_parser_set_input_string(&parser, (const unsigned char*)input,
                                  strlen(input));

    AngaraObject root = ang_nil();
    anchor_count = 0;
    parse_stack_depth = 0;

    yaml_event_t event;
    int done = 0;

    while (!done) {
        if (!yaml_parser_parse(&parser, &event)) {
            char err[512];
            snprintf(err, sizeof(err),
                     "YAML parse error at line %zu, column %zu: %s (%s)",
                     parser.problem_mark.line + 1,
                     parser.problem_mark.column + 1,
                     parser.problem ? (const char*)parser.problem : "unknown error",
                     parser.context ? (const char*)parser.context : "");
            yaml_parser_delete(&parser);
            anchors_clear();
            ang_api->throw_error(err);
            return ang_nil();
        }

        switch (event.type) {

        case YAML_STREAM_START_EVENT:
        case YAML_DOCUMENT_START_EVENT:
            // Ignore — just metadata
            break;

        case YAML_STREAM_END_EVENT:
            done = 1;
            break;

        case YAML_DOCUMENT_END_EVENT:
            // End of a document; if we have a root, we're done
            // (for multi-doc YAML, we take the first doc)
            done = 1;
            break;

        case YAML_SCALAR_EVENT: {
            const char* value = (event.data.scalar.value)
                ? (const char*)event.data.scalar.value : "";
            size_t vlen = event.data.scalar.length;
            yaml_scalar_style_t style = event.data.scalar.style;

            AngaraObject obj = yaml_scalar_to_angara(value, vlen, style);

            // Handle anchor
            if (event.data.scalar.anchor) {
                anchor_set((const char*)event.data.scalar.anchor, obj);
            }

            // Add to parent or set as root
            if (parse_stack_depth == 0) {
                root = obj;
                // No need to incref — root holds the reference
            } else {
                add_to_parent(obj);
            }
            break;
        }

        case YAML_ALIAS_EVENT: {
            const char* anchor_name = (const char*)event.data.alias.anchor;
            AngaraObject aliased = anchor_get(anchor_name);
            if (parse_stack_depth == 0) {
                root = aliased;
            } else {
                add_to_parent(aliased);
            }
            break;
        }

        case YAML_SEQUENCE_START_EVENT: {
            AngaraObject list = ang_api->list_new();

            if (event.data.sequence_start.anchor) {
                anchor_set((const char*)event.data.sequence_start.anchor, list);
            }

            if (parse_stack_depth == 0) {
                root = list;
            } else {
                add_to_parent(list);
            }
            push_stack(list, STACK_SEQUENCE);
            break;
        }

        case YAML_SEQUENCE_END_EVENT:
            pop_stack();
            break;

        case YAML_MAPPING_START_EVENT: {
            AngaraObject record = ang_api->record_new();

            if (event.data.mapping_start.anchor) {
                anchor_set((const char*)event.data.mapping_start.anchor, record);
            }

            if (parse_stack_depth == 0) {
                root = record;
            } else {
                add_to_parent(record);
            }
            push_stack(record, STACK_MAPPING);
            break;
        }

        case YAML_MAPPING_END_EVENT:
            pop_stack();
            break;

        default:
            break;
        }

        yaml_event_delete(&event);
    }

    yaml_parser_delete(&parser);
    anchors_clear();

    // If nothing was parsed, return nil
    if (ang_is_nil(root) && parse_stack_depth == 0)
        return ang_nil();

    return root;
}

// =============================================================================
//  yaml.stringify(any) -> string
// =============================================================================

// Forward declaration for recursion
static void stringify_value(AngaraObject obj, char** buf, size_t* cap,
                            size_t* len, int indent, int is_list_item);

/// Write indentation spaces.
static void write_indent(char** buf, size_t* cap, size_t* len, int indent) {
    size_t needed = *len + (size_t)(indent * 2) + 1;
    buf_grow(buf, cap, needed);
    for (int i = 0; i < indent; i++) {
        (*buf)[(*len)++] = ' ';
        (*buf)[(*len)++] = ' ';
    }
    (*buf)[*len] = '\0';
}

/// Write a raw string to the buffer.
static void write_str(char** buf, size_t* cap, size_t* len, const char* s) {
    size_t slen = strlen(s);
    size_t needed = *len + slen + 1;
    buf_grow(buf, cap, needed);
    memcpy(*buf + *len, s, slen);
    *len += slen;
    (*buf)[*len] = '\0';
}

/// Write a newline.
static void write_nl(char** buf, size_t* cap, size_t* len) {
    size_t needed = *len + 2;
    buf_grow(buf, cap, needed);
    (*buf)[(*len)++] = '\n';
    (*buf)[*len] = '\0';
}

/// Write a YAML double-quoted string (handles escaping).
static void write_quoted(char** buf, size_t* cap, size_t* len, const char* s) {
    size_t needed = *len + strlen(s) * 2 + 4;
    buf_grow(buf, cap, needed);
    (*buf)[(*len)++] = '"';
    for (const char* p = s; *p; p++) {
        switch (*p) {
            case '"':  write_str(buf, cap, len, "\\\""); break;
            case '\\': write_str(buf, cap, len, "\\\\"); break;
            case '\n': write_str(buf, cap, len, "\\n");  break;
            case '\r': write_str(buf, cap, len, "\\r");  break;
            case '\t': write_str(buf, cap, len, "\\t");  break;
            default: {
                // Grow if needed
                size_t need = *len + 2;
                buf_grow(buf, cap, need);
                (*buf)[(*len)++] = *p;
                (*buf)[*len] = '\0';
                break;
            }
        }
    }
    size_t need = *len + 2;
    buf_grow(buf, cap, need);
    (*buf)[(*len)++] = '"';
    (*buf)[*len] = '\0';
}

/// Stringify a scalar (non-container) value.
static void stringify_scalar(AngaraObject obj, char** buf, size_t* cap,
                             size_t* len) {
    if (ang_is_nil(obj)) {
        write_str(buf, cap, len, "null");
    } else if (ang_is_bool(obj)) {
        write_str(buf, cap, len, ang_as_bool(obj) ? "true" : "false");
    } else if (ang_is_i64(obj)) {
        char tmp[32];
        snprintf(tmp, sizeof(tmp), "%lld", (long long)ang_as_i64(obj));
        write_str(buf, cap, len, tmp);
    } else if (ang_is_f64(obj)) {
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "%.17g", ang_as_f64(obj));
        write_str(buf, cap, len, tmp);
    } else if (IS_STR(obj)) {
        const char* s = ang_api->as_cstr(obj);
        size_t slen = strlen(s);
        if (yaml_needs_quotes(s, slen)) {
            write_quoted(buf, cap, len, s);
        } else {
            write_str(buf, cap, len, s);
        }
    } else {
        // Unknown type: convert to string representation
        AngaraObject str = ang_api->to_string(obj);
        const char* s = ang_api->as_cstr(str);
        write_quoted(buf, cap, len, s);
        ang_api->decref(str);
    }
}

/// Stringify a record (mapping).
static void stringify_record(AngaraObject obj, char** buf, size_t* cap,
                             size_t* len, int indent) {
    size_t rlen = ang_api->record_len(obj);

    if (rlen == 0) {
        write_str(buf, cap, len, "{}");
        return;
    }

    for (size_t i = 0; i < rlen; i++) {
        const char* key = ang_api->record_key_at(obj, i);
        AngaraObject val = ang_api->record_val_at(obj, i);

        // Write key
        if (i > 0) write_nl(buf, cap, len);
        write_indent(buf, cap, len, indent);
        if (yaml_needs_quotes(key, strlen(key))) {
            write_quoted(buf, cap, len, key);
        } else {
            write_str(buf, cap, len, key);
        }
        write_str(buf, cap, len, ": ");

        // Write value
        if (IS_REC(val) || IS_LIST(val)) {
            write_nl(buf, cap, len);
            stringify_value(val, buf, cap, len, indent + 1, 0);
        } else {
            stringify_scalar(val, buf, cap, len);
        }

        ang_api->decref(val);
    }
}

/// Stringify a list (sequence).
static void stringify_list(AngaraObject obj, char** buf, size_t* cap,
                           size_t* len, int indent) {
    size_t llen = ang_api->list_len(obj);

    if (llen == 0) {
        write_str(buf, cap, len, "[]");
        return;
    }

    for (size_t i = 0; i < llen; i++) {
        AngaraObject elem = ang_api->list_get(obj, (int64_t)i);

        write_indent(buf, cap, len, indent);
        write_str(buf, cap, len, "- ");

        if (IS_REC(elem) || IS_LIST(elem)) {
            write_nl(buf, cap, len);
            stringify_value(elem, buf, cap, len, indent + 1, 1);
        } else {
            stringify_scalar(elem, buf, cap, len);
        }

        if (i < llen - 1) write_nl(buf, cap, len);
        ang_api->decref(elem);
    }
}

/// Recursively stringify any Angara value.
static void stringify_value(AngaraObject obj, char** buf, size_t* cap,
                            size_t* len, int indent, int is_list_item) {
    (void)is_list_item; // Future use for compact flow-style nesting

    if (IS_REC(obj)) {
        stringify_record(obj, buf, cap, len, indent);
    } else if (IS_LIST(obj)) {
        stringify_list(obj, buf, cap, len, indent);
    } else {
        stringify_scalar(obj, buf, cap, len);
    }
}

AngaraObject Angara_yaml_stringify(int arg_count, AngaraObject args[]) {
    (void)arg_count;

    size_t cap = 4096;
    size_t len = 0;
    char* buf = (char*)malloc(cap);
    buf[0] = '\0';

    stringify_value(args[0], &buf, &cap, &len, 0, 0);

    // Add trailing newline
    write_nl(&buf, &cap, &len);

    return ang_api->string_no_copy(buf, len);
}

// =============================================================================
//  Module exports
// =============================================================================

static const AngaraFuncDef YAML_EXPORTS[] = {
    {"parse",     Angara_yaml_parse,     "s->a", NULL},
    {"stringify", Angara_yaml_stringify, "a->s", NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(yaml) {
    ang_api = api;
    *def_count = (sizeof(YAML_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return YAML_EXPORTS;
}
