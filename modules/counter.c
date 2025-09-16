// =======================================================================
// REPLACE the entire contents of counter.c with this FINAL, GOLD STANDARD version.
// =======================================================================

#include "../src/runtime/angara_runtime.h"
#include <stdlib.h>

typedef struct {
    int64_t value;
} CounterData;

void finalize_counter(void* data) {
    free(data);
}

static inline CounterData* get_data(AngaraObject self) {
    return (CounterData*)AS_NATIVE_INSTANCE(self)->data;
}

// --- Constructor ---
// THE FIX: The C function name must exactly match the mangled name.
// Transpiler generates: Angara_ModuleName_ClassName
AngaraObject Angara_counter_Counter(int arg_count, AngaraObject* args) {
    if (arg_count != 1 || !IS_I64(args[0])) {
        angara_throw_error("Counter constructor expects one integer argument.");
        return angara_create_nil();
    }
    CounterData* data = (CounterData*)malloc(sizeof(CounterData));
    data->value = AS_I64(args[0]);
    return angara_create_native_instance(data, finalize_counter);
}

// --- Methods ---
// THE FIX: The C function name must exactly match the mangled name.
// Transpiler generates: Angara_ClassName_MethodName
AngaraObject Angara_Counter_increment(int arg_count, AngaraObject* args) {
    AngaraObject self = args[0];
    get_data(self)->value++;
    return angara_create_nil();
}

AngaraObject Angara_Counter_value(int arg_count, AngaraObject* args) {
    AngaraObject self = args[0];
    return angara_create_i64(get_data(self)->value);
}


// --- ABI Definitions ---
// The function pointers now point to the correctly named C functions.
static const AngaraMethodDef COUNTER_METHODS[] = {
        {"increment", Angara_Counter_increment, "->n"},
        {"value",     Angara_Counter_value,     "->i"},
        {NULL, NULL, NULL}
};

static const AngaraClassDef COUNTER_CLASS_DEF = {
        "Counter",
        NULL,
        COUNTER_METHODS
};

// The exported function definition.
// Its Angara name is "Counter". The C function it points to is Angara_counter_Counter.
static const AngaraFuncDef COUNTER_EXPORTS[] = {
        { "Counter", Angara_counter_Counter, "i->Counter", &COUNTER_CLASS_DEF },
        { NULL, NULL, NULL, NULL } // Sentinel
};

ANGARA_MODULE_INIT(counter) {
        *def_count = (sizeof(COUNTER_EXPORTS) / sizeof(AngaraFuncDef)) - 1;
        return COUNTER_EXPORTS;
}