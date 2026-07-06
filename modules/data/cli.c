/// Angara cli module — subcommand dispatch and help generation for CLI applications.
///
/// Builds on the low-level `args` module pattern to provide command definitions,
/// automatic --help generation, and subcommand routing.
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "Angara.h"

// ---------------------------------------------------------------------------
// Type-check macros
// ---------------------------------------------------------------------------
#define IS_STR(v)  (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_STRING)
#define IS_REC(v)  (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_RECORD)
#define IS_LIST(v) (ang_is_obj(v) && ang_api->obj_type(v) == ANG_OBJ_LIST)

// ---------------------------------------------------------------------------
// Capacity limits
// ---------------------------------------------------------------------------
#define MAX_COMMANDS 32
#define MAX_OPTIONS  64
#define MAX_FLAGS    64

// ---------------------------------------------------------------------------
// Internal definition structs
// ---------------------------------------------------------------------------
typedef struct {
    char* long_name;       // owned copy, e.g. "port"
    char* short_name;      // owned copy, single char or NULL, e.g. "p"
    char* help;            // owned copy, or NULL
    char* default_value;   // owned copy, or NULL
} OptionDef;

typedef struct {
    char* long_name;       // owned copy, e.g. "verbose"
    char* short_name;      // owned copy, single char or NULL, e.g. "v"
    char* help;            // owned copy, or NULL
} FlagDef;

typedef struct {
    char*       name;      // owned copy, command name
    char*       help;      // owned copy, help text
    OptionDef   options[MAX_OPTIONS];
    int         option_count;
    FlagDef     flags[MAX_FLAGS];
    int         flag_count;
} CommandDef;

typedef struct {
    char*       name;
    char*       version;
    char*       description;
    CommandDef  commands[MAX_COMMANDS];
    int         command_count;
    OptionDef   global_options[MAX_OPTIONS];
    int         global_option_count;
    FlagDef     global_flags[MAX_FLAGS];
    int         global_flag_count;
} CliDef;

// ---------------------------------------------------------------------------
// Helpers — safe string allocation
// ---------------------------------------------------------------------------

/// Portable xstrdup (safe on all POSIX + C99 systems).
static char* xstrdup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char* copy = (char*)malloc(len + 1);
    if (copy) memcpy(copy, s, len + 1);
    return copy;
}

// ---------------------------------------------------------------------------
// Helpers — safe string extraction from AngaraObjects
// ---------------------------------------------------------------------------

/// Extract a string field from a record. Returns owned copy or NULL.
static char* rec_get_str(AngaraObject rec, const char* key) {
    AngaraObject val = ang_api->record_get(rec, key);
    if (ang_is_nil(val) || !IS_STR(val)) {
        ang_api->decref(val);
        return NULL;
    }
    const char* s = ang_api->as_cstr(val);
    char* copy = xstrdup(s);
    ang_api->decref(val);
    return copy;
}

// ---------------------------------------------------------------------------
// Helpers — record building
// ---------------------------------------------------------------------------

static void rec_set_str(AngaraObject rec, const char* key, const char* val) {
    ang_api->record_set(rec, key, ang_api->string(val));
}

static void rec_set_bool(AngaraObject rec, const char* key, int val) {
    ang_api->record_set(rec, key, ang_bool(val));
}

static void list_push_str(AngaraObject list, const char* val) {
    ang_api->list_push(list, ang_api->string(val));
}

// ---------------------------------------------------------------------------
// Definition parser — parse the Angara record into CliDef
// ---------------------------------------------------------------------------

/// Parse an option definition record into an OptionDef.
/// Returns 0 on success, -1 on missing required field.
static int parse_option_def(AngaraObject opt_rec, OptionDef* out) {
    memset(out, 0, sizeof(*out));
    out->long_name = rec_get_str(opt_rec, "long");
    if (!out->long_name) return -1;  // "long" is required
    out->short_name = rec_get_str(opt_rec, "short");
    out->help = rec_get_str(opt_rec, "help");
    out->default_value = rec_get_str(opt_rec, "default");
    return 0;
}

/// Parse a flag definition record into a FlagDef.
/// Returns 0 on success, -1 on missing required field.
static int parse_flag_def(AngaraObject flag_rec, FlagDef* out) {
    memset(out, 0, sizeof(*out));
    out->long_name = rec_get_str(flag_rec, "long");
    if (!out->long_name) return -1;  // "long" is required
    out->short_name = rec_get_str(flag_rec, "short");
    out->help = rec_get_str(flag_rec, "help");
    return 0;
}

/// Parse a list of option/flag definition records into arrays.
static void parse_options_list(AngaraObject list, OptionDef* out, int* count, int max) {
    if (!IS_LIST(list)) return;
    size_t len = ang_api->list_len(list);
    for (size_t i = 0; i < len && *count < max; i++) {
        AngaraObject elem = ang_api->list_get(list, (int64_t)i);
        if (IS_REC(elem)) {
            if (parse_option_def(elem, &out[*count]) == 0) {
                (*count)++;
            }
        }
        ang_api->decref(elem);
    }
}

static void parse_flags_list(AngaraObject list, FlagDef* out, int* count, int max) {
    if (!IS_LIST(list)) return;
    size_t len = ang_api->list_len(list);
    for (size_t i = 0; i < len && *count < max; i++) {
        AngaraObject elem = ang_api->list_get(list, (int64_t)i);
        if (IS_REC(elem)) {
            if (parse_flag_def(elem, &out[*count]) == 0) {
                (*count)++;
            }
        }
        ang_api->decref(elem);
    }
}

/// Parse the full CLI definition record into a CliDef.
/// Returns 0 on success, -1 on error (throws).
static int parse_cli_def(AngaraObject def, CliDef* out) {
    memset(out, 0, sizeof(*out));

    if (!IS_REC(def)) {
        ang_api->throw_error("cli.parse: definition must be a record");
        return -1;
    }

    out->name = rec_get_str(def, "name");
    if (!out->name) {
        ang_api->throw_error("cli.parse: definition must have a 'name' field");
        return -1;
    }
    out->version = rec_get_str(def, "version");
    out->description = rec_get_str(def, "description");

    // Parse global options/flags
    AngaraObject global_opts = ang_api->record_get(def, "options");
    if (!ang_is_nil(global_opts)) {
        parse_options_list(global_opts, out->global_options,
                          &out->global_option_count, MAX_OPTIONS);
    }
    ang_api->decref(global_opts);

    AngaraObject global_flags = ang_api->record_get(def, "flags");
    if (!ang_is_nil(global_flags)) {
        parse_flags_list(global_flags, out->global_flags,
                        &out->global_flag_count, MAX_FLAGS);
    }
    ang_api->decref(global_flags);

    // Parse commands
    AngaraObject cmds = ang_api->record_get(def, "commands");
    if (!ang_is_nil(cmds) && IS_LIST(cmds)) {
        size_t cmd_len = ang_api->list_len(cmds);
        for (size_t i = 0; i < cmd_len && out->command_count < MAX_COMMANDS; i++) {
            AngaraObject cmd_rec = ang_api->list_get(cmds, (int64_t)i);
            if (!IS_REC(cmd_rec)) {
                ang_api->decref(cmd_rec);
                continue;
            }

            CommandDef* cmd = &out->commands[out->command_count];
            memset(cmd, 0, sizeof(*cmd));

            cmd->name = rec_get_str(cmd_rec, "name");
            if (!cmd->name) {
                ang_api->decref(cmd_rec);
                continue;  // skip commands without a name
            }
            cmd->help = rec_get_str(cmd_rec, "help");

            AngaraObject cmd_opts = ang_api->record_get(cmd_rec, "options");
            if (!ang_is_nil(cmd_opts)) {
                parse_options_list(cmd_opts, cmd->options,
                                  &cmd->option_count, MAX_OPTIONS);
            }
            ang_api->decref(cmd_opts);

            AngaraObject cmd_flags = ang_api->record_get(cmd_rec, "flags");
            if (!ang_is_nil(cmd_flags)) {
                parse_flags_list(cmd_flags, cmd->flags,
                                &cmd->flag_count, MAX_FLAGS);
            }
            ang_api->decref(cmd_flags);

            ang_api->decref(cmd_rec);
            out->command_count++;
        }
    }
    ang_api->decref(cmds);

    return 0;
}

/// Free all owned strings in a CliDef.
static void free_cli_def(CliDef* def) {
    free(def->name);
    free(def->version);
    free(def->description);

    for (int i = 0; i < def->global_option_count; i++) {
        free(def->global_options[i].long_name);
        free(def->global_options[i].short_name);
        free(def->global_options[i].help);
        free(def->global_options[i].default_value);
    }
    for (int i = 0; i < def->global_flag_count; i++) {
        free(def->global_flags[i].long_name);
        free(def->global_flags[i].short_name);
        free(def->global_flags[i].help);
    }
    for (int i = 0; i < def->command_count; i++) {
        CommandDef* cmd = &def->commands[i];
        free(cmd->name);
        free(cmd->help);
        for (int j = 0; j < cmd->option_count; j++) {
            free(cmd->options[j].long_name);
            free(cmd->options[j].short_name);
            free(cmd->options[j].help);
            free(cmd->options[j].default_value);
        }
        for (int j = 0; j < cmd->flag_count; j++) {
            free(cmd->flags[j].long_name);
            free(cmd->flags[j].short_name);
            free(cmd->flags[j].help);
        }
    }
}

// ---------------------------------------------------------------------------
// Option/flag lookup helpers
// ---------------------------------------------------------------------------

/// Look up an option by long name (--foo) or short name (-f).
/// Searches command options first, then global options.
/// Returns index in the appropriate array, or -1.
/// Sets *from_global to 1 if found in global array.
static int find_option(CliDef* def, int cmd_idx, const char* long_name,
                       char short_char, int* from_global) {
    *from_global = 0;

    if (cmd_idx >= 0) {
        CommandDef* cmd = &def->commands[cmd_idx];
        for (int i = 0; i < cmd->option_count; i++) {
            if (long_name && cmd->options[i].long_name &&
                strcmp(cmd->options[i].long_name, long_name) == 0)
                return i;
            if (short_char && cmd->options[i].short_name &&
                cmd->options[i].short_name[0] == short_char)
                return i;
        }
    }

    // Search globals
    for (int i = 0; i < def->global_option_count; i++) {
        if (long_name && def->global_options[i].long_name &&
            strcmp(def->global_options[i].long_name, long_name) == 0) {
            *from_global = 1;
            return i;
        }
        if (short_char && def->global_options[i].short_name &&
            def->global_options[i].short_name[0] == short_char) {
            *from_global = 1;
            return i;
        }
    }

    return -1;
}

/// Look up a flag by long or short name. Same search order as options.
static int find_flag(CliDef* def, int cmd_idx, const char* long_name,
                     char short_char, int* from_global) {
    *from_global = 0;

    if (cmd_idx >= 0) {
        CommandDef* cmd = &def->commands[cmd_idx];
        for (int i = 0; i < cmd->flag_count; i++) {
            if (long_name && cmd->flags[i].long_name &&
                strcmp(cmd->flags[i].long_name, long_name) == 0)
                return i;
            if (short_char && cmd->flags[i].short_name &&
                cmd->flags[i].short_name[0] == short_char)
                return i;
        }
    }

    for (int i = 0; i < def->global_flag_count; i++) {
        if (long_name && def->global_flags[i].long_name &&
            strcmp(def->global_flags[i].long_name, long_name) == 0) {
            *from_global = 1;
            return i;
        }
        if (short_char && def->global_flags[i].short_name &&
            def->global_flags[i].short_name[0] == short_char) {
            *from_global = 1;
            return i;
        }
    }

    return -1;
}

/// Find a command by name. Returns index or -1.
static int find_command(CliDef* def, const char* name) {
    for (int i = 0; i < def->command_count; i++) {
        if (def->commands[i].name && strcmp(def->commands[i].name, name) == 0)
            return i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Core: cli.parse(definition, argv) -> result record
// ---------------------------------------------------------------------------

AngaraObject Angara_cli_parse(int arg_count, AngaraObject* args) {
    if (arg_count < 2) {
        ang_api->throw_error("cli.parse(definition, argv) expects two arguments");
        return ang_nil();
    }

    AngaraObject def_obj = args[0];
    AngaraObject argv_obj = args[1];

    if (!IS_LIST(argv_obj)) {
        ang_api->throw_error("cli.parse: argv must be a list of strings");
        return ang_nil();
    }

    // --- Phase 1: Parse the definition ---
    CliDef def;
    if (parse_cli_def(def_obj, &def) != 0) {
        return ang_nil();  // error already thrown
    }

    // --- Phase 2: Build result containers ---
    AngaraObject result_options = ang_api->record_new();
    AngaraObject result_flags    = ang_api->record_new();
    AngaraObject result_args     = ang_api->list_new();
    int help_requested = 0;
    int cmd_idx = -1;
    const char* matched_command = "";

    size_t argv_len = ang_api->list_len(argv_obj);
    int past_separator = 0;
    int command_found = 0;

    // --- Phase 3: Scan argv ---
    for (size_t i = 0; i < argv_len; i++) {
        AngaraObject elem = ang_api->list_get(argv_obj, (int64_t)i);
        if (!IS_STR(elem)) {
            // Non-string values become positional args
            ang_api->list_push(result_args, elem);
            ang_api->decref(elem);
            continue;
        }

        const char* arg = ang_api->as_cstr(elem);
        size_t arg_len = ang_api->str_len(elem);

        // -- separator
        if (!past_separator && strcmp(arg, "--") == 0) {
            past_separator = 1;
            ang_api->decref(elem);
            continue;
        }

        if (past_separator) {
            list_push_str(result_args, arg);
            ang_api->decref(elem);
            continue;
        }

        // --help / -h  (recognised anywhere before --)
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            help_requested = 1;
            ang_api->decref(elem);
            continue;
        }

        // --long-option
        if (arg_len >= 3 && arg[0] == '-' && arg[1] == '-') {
            const char* rest = arg + 2;
            const char* eq = strchr(rest, '=');

            if (eq) {
                // --option=value
                size_t key_len = (size_t)(eq - rest);
                char* key = (char*)malloc(key_len + 1);
                memcpy(key, rest, key_len);
                key[key_len] = '\0';

                int from_global;
                int opt_idx = find_option(&def, cmd_idx, key, 0, &from_global);
                if (opt_idx >= 0) {
                    const OptionDef* opt = from_global
                        ? &def.global_options[opt_idx]
                        : &def.commands[cmd_idx].options[opt_idx];
                    rec_set_str(result_options, opt->long_name, eq + 1);
                } else {
                    // Unknown option — pass through as positional
                    list_push_str(result_args, arg);
                }
                free(key);
            } else {
                // --option or --flag
                int from_global_o, from_global_f;
                int opt_idx = find_option(&def, cmd_idx, rest, 0, &from_global_o);
                int flg_idx = find_flag(&def, cmd_idx, rest, 0, &from_global_f);

                if (opt_idx >= 0) {
                    // It's an option — consume next arg as value
                    const OptionDef* opt = from_global_o
                        ? &def.global_options[opt_idx]
                        : &def.commands[cmd_idx].options[opt_idx];

                    AngaraObject next = (i + 1 < argv_len)
                        ? ang_api->list_get(argv_obj, (int64_t)(i + 1))
                        : ang_nil();
                    const char* next_str = IS_STR(next) ? ang_api->as_cstr(next) : NULL;
                    int next_is_value = next_str && next_str[0] != '\0';

                    if (next_is_value) {
                        rec_set_str(result_options, opt->long_name, next_str);
                        i++;
                    } else {
                        // No value provided — use default or empty
                        if (opt->default_value) {
                            rec_set_str(result_options, opt->long_name, opt->default_value);
                        } else {
                            rec_set_str(result_options, opt->long_name, "");
                        }
                    }
                    ang_api->decref(next);
                } else if (flg_idx >= 0) {
                    // It's a flag
                    const FlagDef* flg = from_global_f
                        ? &def.global_flags[flg_idx]
                        : &def.commands[cmd_idx].flags[flg_idx];
                    rec_set_bool(result_flags, flg->long_name, 1);
                } else {
                    // Unknown — pass through
                    list_push_str(result_args, arg);
                }
            }
            ang_api->decref(elem);
            continue;
        }

        // -short-flags
        if (arg_len >= 2 && arg[0] == '-' && arg[1] != '-') {
            int consumed = 0;

            for (size_t c = 1; c < arg_len; c++) {
                char sc = arg[c];

                int from_global_o, from_global_f;
                int opt_idx = find_option(&def, cmd_idx, NULL, sc, &from_global_o);
                int flg_idx = find_flag(&def, cmd_idx, NULL, sc, &from_global_f);

                if (opt_idx >= 0 && c == arg_len - 1) {
                    // Last char in group, and it matches an option — consume next arg
                    const OptionDef* opt = from_global_o
                        ? &def.global_options[opt_idx]
                        : &def.commands[cmd_idx].options[opt_idx];

                    AngaraObject next = (i + 1 < argv_len)
                        ? ang_api->list_get(argv_obj, (int64_t)(i + 1))
                        : ang_nil();
                    const char* next_str = IS_STR(next) ? ang_api->as_cstr(next) : NULL;
                    int next_is_value = next_str && next_str[0] != '\0';

                    if (next_is_value) {
                        rec_set_str(result_options, opt->long_name, next_str);
                        i++;
                        consumed = 1;
                    } else {
                        if (opt->default_value) {
                            rec_set_str(result_options, opt->long_name, opt->default_value);
                        } else {
                            rec_set_str(result_options, opt->long_name, "");
                        }
                    }
                    ang_api->decref(next);
                } else if (flg_idx >= 0) {
                    const FlagDef* flg = from_global_f
                        ? &def.global_flags[flg_idx]
                        : &def.commands[cmd_idx].flags[flg_idx];
                    rec_set_bool(result_flags, flg->long_name, 1);
                }
                // Unknown short flags are silently ignored
            }

            if (!consumed) {
                ang_api->decref(elem);
            } else {
                ang_api->decref(elem);
            }
            continue;
        }

        // Positional argument — check if it's a command name
        if (!command_found && find_command(&def, arg) >= 0) {
            cmd_idx = find_command(&def, arg);
            matched_command = def.commands[cmd_idx].name;
            command_found = 1;
            ang_api->decref(elem);
            continue;
        }

        // Regular positional argument
        list_push_str(result_args, arg);
        ang_api->decref(elem);
    }

    // --- Phase 4: Apply defaults for unmatched options ---
    // Command options
    if (cmd_idx >= 0) {
        CommandDef* cmd = &def.commands[cmd_idx];
        for (int i = 0; i < cmd->option_count; i++) {
            if (cmd->options[i].default_value) {
                AngaraObject existing = ang_api->record_get(result_options,
                    cmd->options[i].long_name);
                if (ang_is_nil(existing)) {
                    rec_set_str(result_options,
                               cmd->options[i].long_name,
                               cmd->options[i].default_value);
                }
                ang_api->decref(existing);
            }
        }
    }
    // Global options
    for (int i = 0; i < def.global_option_count; i++) {
        if (def.global_options[i].default_value) {
            AngaraObject existing = ang_api->record_get(result_options,
                def.global_options[i].long_name);
            if (ang_is_nil(existing)) {
                rec_set_str(result_options,
                           def.global_options[i].long_name,
                           def.global_options[i].default_value);
            }
            ang_api->decref(existing);
        }
    }

    // --- Phase 5: Build result ---
    AngaraObject result = ang_api->record_new();
    rec_set_str(result, "command", matched_command);
    ang_api->record_set(result, "options", result_options);
    ang_api->record_set(result, "flags", result_flags);
    ang_api->record_set(result, "args", result_args);
    rec_set_bool(result, "help", help_requested);

    ang_api->decref(result_options);
    ang_api->decref(result_flags);
    ang_api->decref(result_args);

    free_cli_def(&def);
    return result;
}

// ---------------------------------------------------------------------------
// cli.help(definition, command_name?) -> string
// ---------------------------------------------------------------------------

/// Dynamic string buffer for building help text.
typedef struct {
    char*  buf;
    size_t len;
    size_t cap;
} StrBuf;

static void sb_init(StrBuf* sb) {
    sb->cap = 512;
    sb->buf = (char*)malloc(sb->cap);
    sb->buf[0] = '\0';
    sb->len = 0;
}

static void sb_grow(StrBuf* sb, size_t needed) {
    while (sb->cap < needed) sb->cap *= 2;
    sb->buf = (char*)realloc(sb->buf, sb->cap);
}

static void sb_append(StrBuf* sb, const char* s) {
    size_t slen = strlen(s);
    size_t needed = sb->len + slen + 1;
    if (needed > sb->cap) sb_grow(sb, needed);
    memcpy(sb->buf + sb->len, s, slen);
    sb->len += slen;
    sb->buf[sb->len] = '\0';
}

static void sb_append_char(StrBuf* sb, char c) {
    size_t needed = sb->len + 2;
    if (needed > sb->cap) sb_grow(sb, needed);
    sb->buf[sb->len++] = c;
    sb->buf[sb->len] = '\0';
}

static void sb_append_padded(StrBuf* sb, const char* left, int left_width,
                              const char* right) {
    sb_append(sb, "  ");
    sb_append(sb, left);
    int pad = left_width - (int)strlen(left);
    if (pad < 0) pad = 0;
    for (int i = 0; i < pad + 2; i++) sb_append_char(sb, ' ');
    sb_append(sb, right);
    sb_append_char(sb, '\n');
}

/// Compute the maximum width needed for a list of names.
static int max_name_width(const char** names, int count) {
    int max = 0;
    for (int i = 0; i < count; i++) {
        int len = (int)strlen(names[i]);
        if (len > max) max = len;
    }
    return max;
}

/// Build the option/flag name string for help display, e.g. "--port, -p PORT"
static void format_opt_name(StrBuf* out, const char* long_name,
                            const char* short_name, int takes_value) {
    sb_append(out, "--");
    sb_append(out, long_name);
    if (short_name) {
        sb_append(out, ", -");
        sb_append(out, short_name);
    }
    if (takes_value) {
        sb_append(out, " VALUE");
    }
}

AngaraObject Angara_cli_help(int arg_count, AngaraObject* args) {
    if (arg_count < 1) {
        ang_api->throw_error("cli.help(definition, command_name?) expects at least one argument");
        return ang_nil();
    }

    AngaraObject def_obj = args[0];
    const char* cmd_filter = NULL;

    if (arg_count >= 2 && IS_STR(args[1])) {
        cmd_filter = ang_api->as_cstr(args[1]);
    }

    CliDef def;
    if (parse_cli_def(def_obj, &def) != 0) {
        return ang_nil();
    }

    StrBuf sb;
    sb_init(&sb);

    // --- Header ---
    sb_append(&sb, def.name ? def.name : "(app)");
    if (def.version) {
        sb_append(&sb, " v");
        sb_append(&sb, def.version);
    }
    if (def.description) {
        sb_append(&sb, " — ");
        sb_append(&sb, def.description);
    }
    sb_append_char(&sb, '\n');
    sb_append_char(&sb, '\n');

    // --- Command-specific help ---
    if (cmd_filter) {
        int ci = find_command(&def, cmd_filter);
        if (ci < 0) {
            sb_append(&sb, "Unknown command: ");
            sb_append(&sb, cmd_filter);
            sb_append_char(&sb, '\n');
        } else {
            CommandDef* cmd = &def.commands[ci];

            // Usage
            sb_append(&sb, "Usage: ");
            sb_append(&sb, def.name ? def.name : "(app)");
            sb_append(&sb, " ");
            sb_append(&sb, cmd->name);
            if (cmd->option_count > 0 || def.global_option_count > 0 ||
                cmd->flag_count > 0 || def.global_flag_count > 0) {
                sb_append(&sb, " [OPTIONS]");
            }
            sb_append(&sb, " [ARGS...]");
            sb_append_char(&sb, '\n');
            sb_append_char(&sb, '\n');

            if (cmd->help) {
                sb_append(&sb, cmd->help);
                sb_append_char(&sb, '\n');
                sb_append_char(&sb, '\n');
            }

            // Build list of all options/flags for this command

            // Collect formatted names for alignment
            const char* opt_names[128];
            const char* opt_helps[128];
            int opt_count = 0;

            for (int i = 0; i < cmd->option_count && opt_count < 128; i++) {
                StrBuf name_buf;
                name_buf.cap = 128;
                name_buf.buf = (char*)malloc(128);
                name_buf.buf[0] = '\0';
                name_buf.len = 0;
                format_opt_name(&name_buf, cmd->options[i].long_name,
                               cmd->options[i].short_name, 1);
                opt_names[opt_count] = xstrdup(name_buf.buf);
                // Build help with default
                if (cmd->options[i].help && cmd->options[i].default_value) {
                    char tmp[512];
                    snprintf(tmp, sizeof(tmp), "%s (default: %s)",
                            cmd->options[i].help, cmd->options[i].default_value);
                    opt_helps[opt_count] = xstrdup(tmp);
                } else if (cmd->options[i].help) {
                    opt_helps[opt_count] = xstrdup(cmd->options[i].help);
                } else if (cmd->options[i].default_value) {
                    char tmp[512];
                    snprintf(tmp, sizeof(tmp), "(default: %s)",
                            cmd->options[i].default_value);
                    opt_helps[opt_count] = xstrdup(tmp);
                } else {
                    opt_helps[opt_count] = xstrdup("");
                }
                free(name_buf.buf);
                opt_count++;
            }

            for (int i = 0; i < def.global_option_count && opt_count < 128; i++) {
                StrBuf name_buf;
                name_buf.cap = 128;
                name_buf.buf = (char*)malloc(128);
                name_buf.buf[0] = '\0';
                name_buf.len = 0;
                format_opt_name(&name_buf, def.global_options[i].long_name,
                               def.global_options[i].short_name, 1);
                opt_names[opt_count] = xstrdup(name_buf.buf);
                if (def.global_options[i].help && def.global_options[i].default_value) {
                    char tmp[512];
                    snprintf(tmp, sizeof(tmp), "%s (default: %s)",
                            def.global_options[i].help, def.global_options[i].default_value);
                    opt_helps[opt_count] = xstrdup(tmp);
                } else {
                    opt_helps[opt_count] = xstrdup(def.global_options[i].help
                                                  ? def.global_options[i].help : "");
                }
                free(name_buf.buf);
                opt_count++;
            }

            for (int i = 0; i < cmd->flag_count && opt_count < 128; i++) {
                StrBuf name_buf;
                name_buf.cap = 128;
                name_buf.buf = (char*)malloc(128);
                name_buf.buf[0] = '\0';
                name_buf.len = 0;
                format_opt_name(&name_buf, cmd->flags[i].long_name,
                               cmd->flags[i].short_name, 0);
                opt_names[opt_count] = xstrdup(name_buf.buf);
                opt_helps[opt_count] = xstrdup(cmd->flags[i].help
                                              ? cmd->flags[i].help : "");
                free(name_buf.buf);
                opt_count++;
            }

            for (int i = 0; i < def.global_flag_count && opt_count < 128; i++) {
                StrBuf name_buf;
                name_buf.cap = 128;
                name_buf.buf = (char*)malloc(128);
                name_buf.buf[0] = '\0';
                name_buf.len = 0;
                format_opt_name(&name_buf, def.global_flags[i].long_name,
                               def.global_flags[i].short_name, 0);
                opt_names[opt_count] = xstrdup(name_buf.buf);
                opt_helps[opt_count] = xstrdup(def.global_flags[i].help
                                              ? def.global_flags[i].help : "");
                free(name_buf.buf);
                opt_count++;
            }

            // Always add --help, -h
            opt_names[opt_count] = xstrdup("--help, -h");
            opt_helps[opt_count] = xstrdup("Show this help message");
            opt_count++;

            if (opt_count > 0) {
                int width = max_name_width(opt_names, opt_count);
                sb_append(&sb, "Options:\n");
                for (int i = 0; i < opt_count; i++) {
                    sb_append_padded(&sb, opt_names[i], width, opt_helps[i]);
                    free((void*)opt_names[i]);
                    free((void*)opt_helps[i]);
                }
            }
        }
    } else {
        // --- Top-level help ---
        sb_append(&sb, "Usage: ");
        sb_append(&sb, def.name ? def.name : "(app)");
        if (def.global_option_count > 0 || def.global_flag_count > 0) {
            sb_append(&sb, " [OPTIONS]");
        }
        if (def.command_count > 0) {
            sb_append(&sb, " COMMAND [ARGS...]");
        }
        sb_append_char(&sb, '\n');
        sb_append_char(&sb, '\n');

        // Commands
        if (def.command_count > 0) {
            const char* cmd_names[64];
            const char* cmd_helps[64];
            for (int i = 0; i < def.command_count && i < 64; i++) {
                cmd_names[i] = def.commands[i].name;
                cmd_helps[i] = def.commands[i].help ? def.commands[i].help : "";
            }

            int width = max_name_width(cmd_names, def.command_count);
            sb_append(&sb, "Commands:\n");
            for (int i = 0; i < def.command_count; i++) {
                sb_append_padded(&sb, cmd_names[i], width, cmd_helps[i]);
            }
            sb_append_char(&sb, '\n');
        }

        // Global options/flags
        int total_items = def.global_option_count + def.global_flag_count + 1; // +1 for --help
        if (total_items > 1) {
            const char* opt_names[128];
            const char* opt_helps[128];
            int opt_count = 0;

            for (int i = 0; i < def.global_option_count && opt_count < 128; i++) {
                StrBuf name_buf = {0};
                name_buf.cap = 128;
                name_buf.buf = (char*)malloc(128);
                name_buf.buf[0] = '\0';
                format_opt_name(&name_buf, def.global_options[i].long_name,
                               def.global_options[i].short_name, 1);
                opt_names[opt_count] = xstrdup(name_buf.buf);
                opt_helps[opt_count] = xstrdup(def.global_options[i].help
                                              ? def.global_options[i].help : "");
                free(name_buf.buf);
                opt_count++;
            }

            for (int i = 0; i < def.global_flag_count && opt_count < 128; i++) {
                StrBuf name_buf = {0};
                name_buf.cap = 128;
                name_buf.buf = (char*)malloc(128);
                name_buf.buf[0] = '\0';
                format_opt_name(&name_buf, def.global_flags[i].long_name,
                               def.global_flags[i].short_name, 0);
                opt_names[opt_count] = xstrdup(name_buf.buf);
                opt_helps[opt_count] = xstrdup(def.global_flags[i].help
                                              ? def.global_flags[i].help : "");
                free(name_buf.buf);
                opt_count++;
            }

            opt_names[opt_count] = xstrdup("--help, -h");
            opt_helps[opt_count] = xstrdup("Show this help message");
            opt_count++;

            int width = max_name_width(opt_names, opt_count);
            sb_append(&sb, "Options:\n");
            for (int i = 0; i < opt_count; i++) {
                sb_append_padded(&sb, opt_names[i], width, opt_helps[i]);
                free((void*)opt_names[i]);
                free((void*)opt_helps[i]);
            }
        }
    }

    free_cli_def(&def);

    AngaraObject result = ang_api->string(sb.buf);
    free(sb.buf);
    return result;
}

// ---------------------------------------------------------------------------
// Accessors for parse results
// ---------------------------------------------------------------------------

/// cli.command(result) -> string
AngaraObject Angara_cli_command(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !IS_REC(args[0])) {
        ang_api->throw_error("cli.command(result) expects a parse result record");
        return ang_nil();
    }
    AngaraObject val = ang_api->record_get(args[0], "command");
    if (ang_is_nil(val)) return ang_api->string("");
    return val;
}

/// cli.option(result, name, default?) -> string
AngaraObject Angara_cli_option(int arg_count, AngaraObject* args) {
    if (arg_count < 2 || !IS_REC(args[0]) || !IS_STR(args[1])) {
        ang_api->throw_error("cli.option(result, name, default?) expects a result record and option name");
        return ang_nil();
    }

    AngaraObject opts = ang_api->record_get(args[0], "options");
    if (ang_is_nil(opts)) {
        return arg_count >= 3 ? args[2] : ang_nil();
    }

    const char* name = ang_api->as_cstr(args[1]);
    AngaraObject val = ang_api->record_get(opts, name);
    ang_api->decref(opts);

    if (ang_is_nil(val)) {
        return arg_count >= 3 ? args[2] : ang_nil();
    }
    return val;
}

/// cli.flag(result, name) -> bool
AngaraObject Angara_cli_flag(int arg_count, AngaraObject* args) {
    if (arg_count < 2 || !IS_REC(args[0]) || !IS_STR(args[1])) {
        ang_api->throw_error("cli.flag(result, name) expects a result record and flag name");
        return ang_nil();
    }

    AngaraObject flags = ang_api->record_get(args[0], "flags");
    if (ang_is_nil(flags)) return ang_bool(false);

    const char* name = ang_api->as_cstr(args[1]);
    AngaraObject val = ang_api->record_get(flags, name);
    ang_api->decref(flags);

    int result = ang_is_bool(val) && ang_as_bool(val);
    ang_api->decref(val);
    return ang_bool(result);
}

/// cli.args(result) -> list<string>
AngaraObject Angara_cli_args(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !IS_REC(args[0])) {
        ang_api->throw_error("cli.args(result) expects a parse result record");
        return ang_nil();
    }

    AngaraObject a = ang_api->record_get(args[0], "args");
    if (ang_is_nil(a)) {
        return ang_api->list_new();
    }
    return a;
}

/// cli.is_help(result) -> bool
AngaraObject Angara_cli_is_help(int arg_count, AngaraObject* args) {
    if (arg_count < 1 || !IS_REC(args[0])) {
        return ang_bool(false);
    }
    AngaraObject val = ang_api->record_get(args[0], "help");
    int result = ang_is_bool(val) && ang_as_bool(val);
    ang_api->decref(val);
    return ang_bool(result);
}

// ---------------------------------------------------------------------------
// Module exports
// ---------------------------------------------------------------------------

static const AngaraFuncDef CLI_EXPORTS[] = {
    {"parse",    Angara_cli_parse,   "{}l<s>->{}", NULL},
    {"help",     Angara_cli_help,    "{}s?->s",    NULL},
    {"command",  Angara_cli_command, "{}->s",      NULL},
    {"option",   Angara_cli_option,  "{}ss?->s",   NULL},
    {"flag",     Angara_cli_flag,    "{}s->b",     NULL},
    {"args",     Angara_cli_args,    "{}->l<s>",   NULL},
    {"is_help",  Angara_cli_is_help, "{}->b",      NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(cli) {
    ang_api = api;
    *def_count = (sizeof(CLI_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
    return CLI_EXPORTS;
}
