/// Angara terminal module — cursor control, key reading, progress bars, and tables.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <errno.h>
#include "Angara.h"

#define IS_STR(v)  (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)
#define IS_LIST(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_LIST)

AngaraObject Angara_term_clear(int arg_count, AngaraObject* args) {
    printf("\033[2J\033[H");
    fflush(stdout);
    return ang_nil();
}

AngaraObject Angara_term_clear_line(int arg_count, AngaraObject* args) {
    printf("\033[2K\r");
    fflush(stdout);
    return ang_nil();
}

AngaraObject Angara_term_set_color(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !IS_STR(args[0])) {
        ang_api->throw_error("set_color(fg, bg?) expects a string.");
        return ang_nil();
    }
    const char* fg = ang_api->as_cstr(args[0]);

    static const char* fg_names[] = {
        "black","red","green","yellow","blue","magenta","cyan","white",
        "bright_black","bright_red","bright_green","bright_yellow",
        "bright_blue","bright_magenta","bright_cyan","bright_white", NULL
    };
    static const int fg_codes[] = {
        30,31,32,33,34,35,36,37,90,91,92,93,94,95,96,97
    };

    int fg_code = -1;
    for (int i = 0; fg_names[i]; i++) {
        if (strcmp(fg, fg_names[i]) == 0) { fg_code = fg_codes[i]; break; }
    }

    if (strcmp(fg, "reset") == 0) {
        printf("\033[0m");
        fflush(stdout);
        return ang_nil();
    }

    if (fg_code < 0) {
        ang_api->throw_error("set_color: unknown color name.");
        return ang_nil();
    }

    if (arg_count >= 2 && IS_STR(args[1])) {
        const char* bg = ang_api->as_cstr(args[1]);
        int bg_code = -1;
        static const char* bg_names[] = {
            "black","red","green","yellow","blue","magenta","cyan","white",
            "bright_black","bright_red","bright_green","bright_yellow",
            "bright_blue","bright_magenta","bright_cyan","bright_white", NULL
        };
        static const int bg_codes[] = {
            40,41,42,43,44,45,46,47,100,101,102,103,104,105,106,107
        };
        for (int i = 0; bg_names[i]; i++) {
            if (strcmp(bg, bg_names[i]) == 0) { bg_code = bg_codes[i]; break; }
        }
        if (bg_code >= 0) {
            printf("\033[%d;%dm", fg_code, bg_code);
        } else {
            printf("\033[%dm", fg_code);
        }
    } else {
        printf("\033[%dm", fg_code);
    }
    fflush(stdout);
    return ang_nil();
}

AngaraObject Angara_term_reset_color(int arg_count, AngaraObject* args) {
    printf("\033[0m");
    fflush(stdout);
    return ang_nil();
}

AngaraObject Angara_term_bold(int arg_count, AngaraObject* args) {
    if (arg_count >= 1 && IS_STR(args[0])) {
        const char* s = ang_api->as_cstr(args[0]);
        size_t len = ang_api->str_len(args[0]);
        char* buf = (char*)malloc(len + 8);
        size_t n = (size_t)snprintf(buf, len + 8, "\033[1m%s\033[0m", s);
        return ang_api->string_no_copy(buf, n);
    }
    printf("\033[1m");
    fflush(stdout);
    return ang_nil();
}

AngaraObject Angara_term_dim(int arg_count, AngaraObject* args) {
    if (arg_count >= 1 && IS_STR(args[0])) {
        const char* s = ang_api->as_cstr(args[0]);
        size_t len = ang_api->str_len(args[0]);
        char* buf = (char*)malloc(len + 8);
        size_t n = (size_t)snprintf(buf, len + 8, "\033[2m%s\033[0m", s);
        return ang_api->string_no_copy(buf, n);
    }
    printf("\033[2m");
    fflush(stdout);
    return ang_nil();
}

AngaraObject Angara_term_underline(int arg_count, AngaraObject* args) {
    if (arg_count >= 1 && IS_STR(args[0])) {
        const char* s = ang_api->as_cstr(args[0]);
        size_t len = ang_api->str_len(args[0]);
        char* buf = (char*)malloc(len + 8);
        size_t n = (size_t)snprintf(buf, len + 8, "\033[4m%s\033[0m", s);
        return ang_api->string_no_copy(buf, n);
    }
    printf("\033[4m");
    fflush(stdout);
    return ang_nil();
}

AngaraObject Angara_term_cursor_move(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !ang_is_i64(args[0]) || !ang_is_i64(args[1])) {
        ang_api->throw_error("cursor_move(row, col) expects two i64 arguments.");
        return ang_nil();
    }
    printf("\033[%lld;%lldH", (long long)ang_as_i64(args[0]), (long long)ang_as_i64(args[1]));
    fflush(stdout);
    return ang_nil();
}

AngaraObject Angara_term_cursor_up(int arg_count, AngaraObject* args) {
    int n = 1;
    if (arg_count >= 1 && ang_is_i64(args[0])) n = (int)ang_as_i64(args[0]);
    printf("\033[%dA", n);
    fflush(stdout);
    return ang_nil();
}

AngaraObject Angara_term_cursor_down(int arg_count, AngaraObject* args) {
    int n = 1;
    if (arg_count >= 1 && ang_is_i64(args[0])) n = (int)ang_as_i64(args[0]);
    printf("\033[%dB", n);
    fflush(stdout);
    return ang_nil();
}

AngaraObject Angara_term_hide_cursor(int arg_count, AngaraObject* args) {
    printf("\033[?25l");
    fflush(stdout);
    return ang_nil();
}

AngaraObject Angara_term_show_cursor(int arg_count, AngaraObject* args) {
    printf("\033[?25h");
    fflush(stdout);
    return ang_nil();
}

AngaraObject Angara_term_terminal_size(int arg_count, AngaraObject* args) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0 && ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) != 0) {
        ws.ws_row = 24;
        ws.ws_col = 80;
    }
    AngaraObject rec = ang_api->record_new();
    ang_api->record_set(rec, "rows", ang_i64(ws.ws_row));
    ang_api->record_set(rec, "cols", ang_i64(ws.ws_col));
    return rec;
}

AngaraObject Angara_term_is_tty(int arg_count, AngaraObject* args) {
    int fd = STDOUT_FILENO;
    if (arg_count >= 1 && ang_is_i64(args[0])) fd = (int)ang_as_i64(args[0]);
    return ang_bool(isatty(fd));
}

AngaraObject Angara_term_read_key(int arg_count, AngaraObject* args) {
    struct termios old_tio, new_tio;
    tcgetattr(STDIN_FILENO, &old_tio);
    new_tio = old_tio;
    new_tio.c_lflag &= ~(ICANON | ECHO);
    new_tio.c_cc[VMIN] = 1;
    new_tio.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);

    char buf[8] = {0};
    ssize_t n = read(STDIN_FILENO, buf, 1);
    if (n <= 0) {
        tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
        return ang_nil();
    }

    if (buf[0] == '\033') {
        new_tio.c_cc[VMIN] = 0;
        new_tio.c_cc[VTIME] = 1;
        tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);

        char seq[7] = {0};
        seq[0] = buf[0];
        int slen = 1;
        ssize_t r = read(STDIN_FILENO, seq + 1, 1);
        if (r > 0) {
            slen++;
            if (seq[1] == '[') {
                r = read(STDIN_FILENO, seq + 2, 1);
                if (r > 0) {
                    slen++;
                    if (seq[2] >= '0' && seq[2] <= '9') {
                        r = read(STDIN_FILENO, seq + 3, 1);
                        if (r > 0) slen++;
                    }
                }
            }
            tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);

            if (slen >= 3 && seq[1] == '[') {
                switch (seq[2]) {
                    case 'A': return ang_api->string("up");
                    case 'B': return ang_api->string("down");
                    case 'C': return ang_api->string("right");
                    case 'D': return ang_api->string("left");
                    case 'H': return ang_api->string("home");
                    case 'F': return ang_api->string("end");
                    case '3': return ang_api->string("delete");
                    case '5': return ang_api->string("pageup");
                    case '6': return ang_api->string("pagedown");
                }
            }
            return ang_api->string("escape");
        }
        tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
        return ang_api->string("escape");
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);

    if (buf[0] == 10 || buf[0] == 13) return ang_api->string("enter");
    if (buf[0] == 9)  return ang_api->string("tab");
    if (buf[0] == 127 || buf[0] == 8) return ang_api->string("backspace");
    if (buf[0] == 4)  return ang_api->string("eof");
    if (buf[0] == 3)  return ang_api->string("ctrl_c");
    if (buf[0] == 26) return ang_api->string("ctrl_z");

    if (buf[0] >= 1 && buf[0] <= 26) {
        char name[8];
        snprintf(name, sizeof(name), "ctrl_%c", 'a' + buf[0] - 1);
        return ang_api->string(name);
    }

    return ang_api->string(buf);
}

AngaraObject Angara_term_progress_bar(int arg_count, AngaraObject* args) {
    if (arg_count < 2 || !ang_is_f64(args[0]) || !ang_is_f64(args[1])) {
        ang_api->throw_error("progress_bar(current, total, width?) expects two f64 arguments.");
        return ang_nil();
    }
    double current = ang_as_f64(args[0]);
    double total = ang_as_f64(args[1]);
    int width = 40;
    if (arg_count >= 3 && ang_is_i64(args[2])) width = (int)ang_as_i64(args[2]);
    if (width < 4) width = 4;
    if (width > 200) width = 200;

    double pct = (total > 0) ? (current / total) : 0.0;
    if (pct < 0.0) pct = 0.0;
    if (pct > 1.0) pct = 1.0;

    int filled = (int)(pct * (width - 3));
    if (filled < 0) filled = 0;
    if (filled > width - 3) filled = width - 3;

    int bar_width = width - 5;
    if (bar_width < 3) bar_width = 3;
    filled = (int)(pct * bar_width);

    char* buf = (char*)malloc(width + 32);
    int pos = 0;
    buf[pos++] = '[';
    for (int i = 0; i < bar_width; i++) {
        if (i < filled) buf[pos++] = '=';
        else if (i == filled) buf[pos++] = '>';
        else buf[pos++] = ' ';
    }
    buf[pos++] = ']';
    pos += snprintf(buf + pos, 8, " %3.0f%%", pct * 100.0);
    return ang_api->string_no_copy(buf, (size_t)pos);
}

AngaraObject Angara_term_colorize(int arg_count, AngaraObject* args) {
    if (arg_count < 2 || !IS_STR(args[0]) || !IS_STR(args[1])) {
        ang_api->throw_error("colorize(text, fg, bg?) expects strings.");
        return ang_nil();
    }

    static const char* color_names[] = {
        "black","red","green","yellow","blue","magenta","cyan","white",
        "bright_black","bright_red","bright_green","bright_yellow",
        "bright_blue","bright_magenta","bright_cyan","bright_white", NULL
    };
    static const int fg_codes[] = {30,31,32,33,34,35,36,37,90,91,92,93,94,95,96,97};

    const char* text = ang_api->as_cstr(args[0]);
    size_t text_len = ang_api->str_len(args[0]);
    const char* fg_name = ang_api->as_cstr(args[1]);

    int fg = -1;
    for (int i = 0; color_names[i]; i++) {
        if (strcmp(fg_name, color_names[i]) == 0) { fg = fg_codes[i]; break; }
    }
    if (fg < 0) { ang_api->incref(args[0]); return args[0]; }

    int bg = -1;
    static const int bg_codes[] = {40,41,42,43,44,45,46,47,100,101,102,103,104,105,106,107};
    if (arg_count >= 3 && IS_STR(args[2])) {
        const char* bg_name = ang_api->as_cstr(args[2]);
        for (int i = 0; color_names[i]; i++) {
            if (strcmp(bg_name, color_names[i]) == 0) { bg = bg_codes[i]; break; }
        }
    }

    size_t cap = text_len + 32;
    char* buf = (char*)malloc(cap);
    int pos;
    if (bg >= 0) pos = snprintf(buf, cap, "\033[%d;%dm", fg, bg);
    else pos = snprintf(buf, cap, "\033[%dm", fg);
    memcpy(buf + pos, text, text_len);
    pos += (int)text_len;
    memcpy(buf + pos, "\033[0m", 4);
    pos += 4;
    return ang_api->string_no_copy(buf, (size_t)pos);
}

AngaraObject Angara_term_table(int arg_count, AngaraObject* args) {
    if (arg_count != 2 || !IS_LIST(args[0]) || !IS_LIST(args[1])) {
        ang_api->throw_error("table(headers, rows) expects two lists.");
        return ang_nil();
    }

    size_t ncols = ang_api->list_len(args[0]);
    if (ncols == 0) return ang_api->string("");

    size_t* widths = (size_t*)calloc(ncols, sizeof(size_t));

    for (size_t c = 0; c < ncols; c++) {
        AngaraObject h = ang_api->list_get(args[0], (int64_t)c);
        if (IS_STR(h)) {
            size_t l = ang_api->str_len(h);
            if (l > widths[c]) widths[c] = l;
        }
        ang_api->decref(h);
    }

    size_t nrows = ang_api->list_len(args[1]);
    for (size_t r = 0; r < nrows; r++) {
        AngaraObject row = ang_api->list_get(args[1], (int64_t)r);
        if (!IS_LIST(row)) { ang_api->decref(row); continue; }
        size_t row_len = ang_api->list_len(row);
        for (size_t c = 0; c < ncols && c < row_len; c++) {
            AngaraObject cell = ang_api->list_get(row, (int64_t)c);
            if (IS_STR(cell)) {
                size_t l = ang_api->str_len(cell);
                if (l > widths[c]) widths[c] = l;
            }
            ang_api->decref(cell);
        }
        ang_api->decref(row);
    }

    size_t total = (ncols * 30 + nrows * ncols * 50) + 256;
    char* buf = (char*)malloc(total);
    size_t pos = 0;

    #define EMIT_STR(s) do { size_t sl = strlen(s); memcpy(buf+pos, s, sl); pos += sl; } while(0)
    #define EMIT_PAD(w) do { for (size_t _p=0;_p<(w);_p++) buf[pos++]=' '; } while(0)

    for (size_t c = 0; c < ncols; c++) {
        AngaraObject h = ang_api->list_get(args[0], (int64_t)c);
        const char* s = IS_STR(h) ? ang_api->as_cstr(h) : "";
        size_t sl = strlen(s);
        memcpy(buf + pos, s, sl); pos += sl;
        EMIT_PAD(widths[c] - sl);
        if (c + 1 < ncols) EMIT_STR("  ");
        ang_api->decref(h);
    }
    EMIT_STR("\n");

    for (size_t c = 0; c < ncols; c++) {
        for (size_t i = 0; i < widths[c]; i++) buf[pos++] = '-';
        if (c + 1 < ncols) EMIT_STR("  ");
    }
    EMIT_STR("\n");

    for (size_t r = 0; r < nrows; r++) {
        AngaraObject row = ang_api->list_get(args[1], (int64_t)r);
        if (!IS_LIST(row)) { ang_api->decref(row); continue; }
        size_t row_len = ang_api->list_len(row);
        for (size_t c = 0; c < ncols; c++) {
            AngaraObject cell = (c < row_len) ? ang_api->list_get(row, (int64_t)c) : ang_api->string("");
            const char* s = IS_STR(cell) ? ang_api->as_cstr(cell) : "";
            size_t sl = strlen(s);
            memcpy(buf + pos, s, sl); pos += sl;
            EMIT_PAD(widths[c] - sl);
            if (c + 1 < ncols) EMIT_STR("  ");
            ang_api->decref(cell);
        }
        EMIT_STR("\n");
        ang_api->decref(row);
    }

    #undef EMIT_STR
    #undef EMIT_PAD

    free(widths);
    buf[pos] = '\0';
    return ang_api->string_no_copy(buf, pos);
}

static const AngaraFuncDef TERM_EXPORTS[] = {
    {"clear",        Angara_term_clear,        "->n",        NULL},
    {"clear_line",   Angara_term_clear_line,   "->n",        NULL},
    {"set_color",    Angara_term_set_color,    "ss?->n",     NULL},
    {"reset_color",  Angara_term_reset_color,  "->n",        NULL},
    {"bold",         Angara_term_bold,         "s?->s",      NULL},
    {"dim",          Angara_term_dim,          "s?->s",      NULL},
    {"underline",    Angara_term_underline,    "s?->s",      NULL},
    {"colorize",     Angara_term_colorize,     "sss?->s",    NULL},
    {"cursor_move",  Angara_term_cursor_move,  "ii->n",      NULL},
    {"cursor_up",    Angara_term_cursor_up,    "i?->n",      NULL},
    {"cursor_down",  Angara_term_cursor_down,  "i?->n",      NULL},
    {"hide_cursor",  Angara_term_hide_cursor,  "->n",        NULL},
    {"show_cursor",  Angara_term_show_cursor,  "->n",        NULL},
    {"terminal_size",Angara_term_terminal_size, "->{}",       NULL},
    {"is_tty",       Angara_term_is_tty,       "i?->b",      NULL},
    {"read_key",     Angara_term_read_key,     "->s",        NULL},
    {"progress_bar", Angara_term_progress_bar, "ddi?->s",    NULL},
    {"table",        Angara_term_table,        "l<s>l<l<s>>->s", NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(term) {
    ang_api = api;
    *def_count = (sizeof(TERM_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return TERM_EXPORTS;
}