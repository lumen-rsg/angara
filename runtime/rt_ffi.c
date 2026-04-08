//
// Angara Runtime — FFI boxing/unboxing, closures, bound methods, call dispatch
//

#include "rt_internal.h"

// --- Closure Operations ---
AngaraObject angara_closure_new(GenericAngaraFn fn, int arity, bool is_native) {
    AngaraClosure* closure = (AngaraClosure*)malloc(sizeof(AngaraClosure));
    closure->obj.type = OBJ_CLOSURE;
    closure->obj.ref_count = 1;
    closure->fn = fn;
    closure->arity = arity;
    closure->is_native = is_native;
    return (AngaraObject){VAL_OBJ, {.obj = (Object*)closure}};
}

AngaraObject angara_call(AngaraObject callee, int arg_count, AngaraObject args[]) {
    if (!IS_OBJ(callee)) {
        angara_throw_error("Runtime Error: Attempted to call a non-function value (not an object).");
        return angara_create_nil();
    }

    // Handle Closure (Standard Function)
    if (OBJ_TYPE(callee) == OBJ_CLOSURE) {
        AngaraClosure* closure = AS_CLOSURE(callee);

        if (!closure->is_native && closure->arity != -1 && closure->arity != arg_count) {
            char error_buf[256];
            snprintf(error_buf, sizeof(error_buf),
                     "Runtime Error: Arity mismatch. Function expected %d argument(s) but received %d.",
                     closure->arity, arg_count);
            angara_throw_error(error_buf);
            return angara_create_nil();
        }

        return closure->fn(arg_count, args);
    }

    // Handle Bound Method
    if (OBJ_TYPE(callee) == OBJ_BOUND_METHOD) {
        AngaraBoundMethod* bm = (AngaraBoundMethod*)AS_OBJ(callee);

        int new_count = arg_count + 1;
        AngaraObject* new_args = (AngaraObject*)malloc(sizeof(AngaraObject) * new_count);
        if (!new_args) {
            angara_throw_error("Out of memory calling bound method.");
            return angara_create_nil();
        }

        new_args[0] = bm->receiver;
        for (int i = 0; i < arg_count; ++i) {
            new_args[i + 1] = args[i];
        }

        AngaraObject result = angara_call(bm->method_closure, new_count, new_args);
        free(new_args);
        return result;
    }

    angara_throw_error("Runtime Error: Attempted to call a non-function value (unknown object type).");
    return angara_create_nil();
}

// --- Bound Method ---
AngaraObject angara_bound_method_new(AngaraObject receiver, AngaraObject method_closure) {
    AngaraBoundMethod* bm = (AngaraBoundMethod*)malloc(sizeof(AngaraBoundMethod));
    bm->obj.type = OBJ_BOUND_METHOD;
    bm->obj.ref_count = 1;
    bm->receiver = receiver;
    bm->method_closure = method_closure;
    angara_incref(receiver);
    angara_incref(method_closure);
    return (AngaraObject){VAL_OBJ, {.obj = (Object*)bm}};
}

// --- FFI Boxing (C → Angara) ---
AngaraObject angara_from_c_i32(int32_t value)  { return angara_create_i64((int64_t)value); }
AngaraObject angara_from_c_u32(uint32_t value)  { return angara_create_i64((int64_t)value); }
AngaraObject angara_from_c_i64(int64_t value)   { return angara_create_i64(value); }
AngaraObject angara_from_c_f64(double value)    { return angara_create_f64(value); }
AngaraObject angara_from_c_bool(bool value)     { return angara_create_bool(value); }
AngaraObject angara_from_c_string(const char* value) { return angara_string_from_c(value); }
AngaraObject angara_from_c_c_ptr(void* value)   { return angara_create_i64((int64_t)value); }
AngaraObject angara_from_c_u64(uint64_t value)  { return angara_create_i64((int64_t)value); }
AngaraObject angara_from_c_object(void* ptr)    { return (AngaraObject){VAL_OBJ, {.obj = (Object*)ptr}}; }

// --- Retype (c_ptr → data instance wrapper) ---
AngaraObject angara_retype_c_ptr(AngaraObject c_ptr_obj, size_t wrapper_size) {
    if (c_ptr_obj.type != VAL_I64 && c_ptr_obj.type != VAL_NIL) {
        return angara_create_nil();
    }

    Object* wrapper_obj = (Object*)malloc(wrapper_size);
    if (!wrapper_obj) {
        angara_throw_error("Out of memory during retype operation.");
        return angara_create_nil();
    }

    wrapper_obj->type = OBJ_DATA_INSTANCE;
    wrapper_obj->ref_count = 1;

    // Store the raw C pointer in the first field after the Object header
    void** ptr_field = (void**)((char*)wrapper_obj + sizeof(Object));
    *ptr_field = (void*)AS_I64(c_ptr_obj);

    return (AngaraObject){VAL_OBJ, {.obj = wrapper_obj}};
}

// --- Native Instance Constructor ---
AngaraObject angara_create_native_instance(void* data, AngaraFinalizerFn finalizer, const char* type_name) {
    AngaraNativeInstance* instance = (AngaraNativeInstance*)malloc(sizeof(AngaraNativeInstance));
    instance->obj.type = OBJ_NATIVE_INSTANCE;
    instance->obj.ref_count = 1;
    instance->data = data;
    instance->finalizer = finalizer;
    instance->type_name = type_name;
    return (AngaraObject){VAL_OBJ, {.obj = (Object*)instance}};
}

// --- Increment / Decrement Operators ---
AngaraObject angara_pre_increment(AngaraObject* lvalue) {
    lvalue->as.i64++;
    angara_incref(*lvalue);
    return *lvalue;
}

AngaraObject angara_post_increment(AngaraObject* lvalue) {
    AngaraObject original_value = angara_create_i64(lvalue->as.i64);
    lvalue->as.i64++;
    return original_value;
}

AngaraObject angara_pre_decrement(AngaraObject* lvalue) {
    lvalue->as.i64--;
    angara_incref(*lvalue);
    return *lvalue;
}

AngaraObject angara_post_decrement(AngaraObject* lvalue) {
    AngaraObject original_value = angara_create_i64(lvalue->as.i64);
    lvalue->as.i64--;
    return original_value;
}