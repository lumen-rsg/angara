//
// Angara Runtime — Internal Header
// Shared declarations for the split runtime implementation files.
//

#ifndef RT_INTERNAL_H
#define RT_INTERNAL_H

#include "angara_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// --- ANSI Color Codes for Error Reporting ---
#define ANSI_COLOR_BOLD_RED   "\033[1;31m"
#define ANSI_COLOR_YELLOW     "\033[0;33m"
#define ANSI_COLOR_CYAN       "\033[0;36m"
#define ANSI_COLOR_RESET      "\033[0m"

// --- Internal Forward Declarations ---

// Memory management (rt_core.c)
void free_object(Object* object);

// Printing (rt_print.c)
void printObject(AngaraObject obj);

// StringBuilder (rt_string.c)
typedef struct {
    char* buffer;
    size_t length;
    size_t capacity;
} StringBuilder;

void sb_init(StringBuilder* sb);
void sb_append(StringBuilder* sb, const char* str);
AngaraObject sb_to_string_obj(StringBuilder* sb);

// List internal growth (rt_list.c)
void grow_list_capacity(AngaraList* list);

// Record internal growth (rt_record.c)
void grow_record_capacity(AngaraRecord* record);

#endif // RT_INTERNAL_H