//
// Angara Runtime — Exception handling
//

#include "rt_internal.h"
#include <setjmp.h>


// --- Global Exception State ---
ExceptionFrame* g_exception_chain_head = NULL;
AngaraObject g_current_exception;

// Frame stack for LLVM backend's try_begin/end (setjmp lives in C, not LLVM IR)
#define ANGARA_MAX_TRY_DEPTH 64
static ExceptionFrame angara_frame_stack[ANGARA_MAX_TRY_DEPTH];
static int angara_frame_top = 0;

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

// --- Try/Catch Mechanism (for LLVM backend) ---
// Uses g_exception_chain_head so angara_throw() can longjmp back.
int angara_try_begin(void) {
    if (angara_frame_top >= ANGARA_MAX_TRY_DEPTH) {
        fprintf(stderr, "Exception frame stack overflow!\n");
        exit(1);
    }
    ExceptionFrame* frame = &angara_frame_stack[angara_frame_top++];
    frame->prev = g_exception_chain_head;
    g_exception_chain_head = frame;

    int result = _setjmp(frame->buffer);
    if (result != 0) {
        // longjmp returned here — angara_throw already popped the frame
        // and set g_current_exception. Decrement top since we won't call
        // try_end for this frame (it was popped by angara_throw).
        angara_frame_top--;
    }
    return result;
}

void angara_try_end(void) {
    // Normal path: pop our frame from the chain
    if (g_exception_chain_head != NULL &&
        g_exception_chain_head == &angara_frame_stack[angara_frame_top - 1]) {
        g_exception_chain_head = g_exception_chain_head->prev;
    }
    if (angara_frame_top > 0) angara_frame_top--;
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