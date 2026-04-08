//
// Angara Runtime — Printing and display
//

#include "rt_internal.h"

// --- Print an AngaraObject to stdout ---
void printObject(AngaraObject obj) {
    switch (obj.type) {
        case VAL_NIL:   printf("nil"); break;
        case VAL_BOOL:  printf(AS_BOOL(obj) ? "true" : "false"); break;
        case VAL_I64:   printf("%lld", AS_I64(obj)); break;
        case VAL_F64:   printf("%g", AS_F64(obj)); break;
        case VAL_OBJ:
            switch (OBJ_TYPE(obj)) {
                case OBJ_STRING:
                    printf("%s", AS_CSTRING(obj));
                    break;
                case OBJ_LIST: {
                    AngaraList* list = AS_LIST(obj);
                    printf("[");
                    for (size_t i = 0; i < list->count; i++) {
                        printObject(list->elements[i]);
                        if (i < list->count - 1) printf(", ");
                    }
                    printf("]");
                    break;
                }
                case OBJ_RECORD: {
                    AngaraRecord* record = AS_RECORD(obj);
                    printf("{");
                    for (size_t i = 0; i < record->count; i++) {
                        printf("%s: ", record->entries[i].key);
                        printObject(record->entries[i].value);
                        if (i < record->count - 1) printf(", ");
                    }
                    printf("}");
                    break;
                }
                case OBJ_DATA_INSTANCE:
                    printf("<data object>");
                    break;
                case OBJ_CLASS:
                    printf("<class %s>", AS_CLASS(obj)->name);
                    break;
                case OBJ_INSTANCE:
                    printf("<instance of %s>", AS_INSTANCE(obj)->klass->name);
                    break;
                case OBJ_THREAD:
                    printf("<thread>");
                    break;
                case OBJ_MUTEX:
                    printf("<mutex>");
                    break;
                case OBJ_BOUND_METHOD:
                    printf("<bound method>");
                    break;
                case OBJ_EXCEPTION: {
                    AngaraException* exc = AS_EXCEPTION(obj);
                    printf("Exception: %s", AS_CSTRING(exc->message));
                    break;
                }
                case OBJ_ENUM_INSTANCE:
                    printf("<enum instance>");
                    break;
                case OBJ_NATIVE_INSTANCE:
                    printf("<native instance at %p>", AS_NATIVE_INSTANCE(obj)->data);
                    break;
                default:
                    printf("<object>");
                    break;
            }
            break;
    }
}

// --- Built-in print function ---
void angara_print(int arg_count, AngaraObject args[]) {
    for (int i = 0; i < arg_count; ++i) {
        printObject(args[i]);
        if (i < arg_count - 1) printf(" ");
    }
    printf("\n");
}