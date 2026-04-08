//
// Angara Runtime — Exception handling
//

#include "rt_internal.h"

// --- Global Exception State ---
ExceptionFrame* g_exception_chain_head = NULL;
AngaraObject g_current_exception;
jmp_buf g_exception_stack[ANGARA_MAX_EXCEPTION_FRAMES];
int g_exception_stack_top = 0;

// --- Exception Object Constructor ---
AngaraObject angara_exception_new(AngaraObject message) {
    if (!IS_STRING(message)) {
        message = angara_string_from_c("Non-string value provided to Exception constructor.");
    }

    AngaraException* exc = (AngaraException*)malloc(sizeof(AngaraException));
    exc->obj.type = OBJ_EXCEPTION;
    exc->obj.ref_count = 1;
    exc->message = message;
    angara_incref(message);

    return (AngaraObject){VAL_OBJ, {.obj = (Object*)exc}};
}

// --- Try/Catch Mechanism ---
int angara_try_begin(void) {
    if (g_exception_stack_top >= ANGARA_MAX_EXCEPTION_FRAMES) {
        fprintf(stderr, "Exception stack overflow!\n");
        exit(1);
    }
    return setjmp(g_exception_stack[g_exception_stack_top++]);
}

void angara_try_end(void) {
    g_exception_stack_top--;
}

// --- Throwing ---
void angara_throw(AngaraObject exception) {
    if (g_exception_chain_head == NULL) {
        // Unhandled exception path
        fprintf(stderr, "\n" ANSI_COLOR_BOLD_RED "[FATAL] Unhandled Angara Exception" ANSI_COLOR_RESET "\n");

        if (IS_OBJ(exception) && OBJ_TYPE(exception) == OBJ_EXCEPTION) {
            fprintf(stderr, ANSI_COLOR_YELLOW "  -> Message: " ANSI_COLOR_RESET "%s\n",
                    AS_CSTRING(((AngaraException*)exception.as.obj)->message));
        } else {
            fprintf(stderr, ANSI_COLOR_YELLOW "  -> Thrown object was not a standard Exception." ANSI_COLOR_RESET "\n");
        }

        fprintf(stderr, ANSI_COLOR_CYAN "  -> No active `try` blocks were found on the call stack. Terminating program." ANSI_COLOR_RESET "\n\n");
        exit(1);
    }

    // Handled exception path
    g_current_exception = exception;
    angara_incref(g_current_exception);

    ExceptionFrame* frame = g_exception_chain_head;
    g_exception_chain_head = frame->prev;

    longjmp(frame->buffer, 1);
}

void angara_throw_error(const char* message) {
    AngaraObject message_obj = angara_string_from_c(message);
    AngaraObject exception_obj = angara_exception_new(message_obj);

    // exception_new took ownership, so decref our local handle
    angara_decref(message_obj);

    angara_throw(exception_obj);
}