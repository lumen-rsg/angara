#pragma once
// AngaraABI.h — Minimal ABI types for native module loading.
// This is a transitional header; will be replaced by the full Angara.h API.
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

namespace angara { // Forward declarations only — not needed for C module loading
    // The compiler uses these internally via dlsym, not by value.
}

// --- Forward declarations for C ABI compatibility ---
// These types must match the layout that native modules export.

typedef struct AngaraObject AngaraObject;

// Function signature for native module functions.
typedef AngaraObject (*AngaraGlobalFn)(int arg_count, AngaraObject* args);
typedef AngaraObject (*AngaraMethodFn)(int arg_count, AngaraObject* args);

// Describes an exported global function or constructor.
typedef struct AngaraFuncDef {
    const char*         name;
    AngaraGlobalFn      function;
    const char*         type_string;
    const struct AngaraClassDef* constructs;
} AngaraFuncDef;

// Describes a method on a native class.
typedef struct {
    const char*     name;
    AngaraMethodFn  function;
    const char*     type_string;
} AngaraMethodDef;

// Describes a field on a native class.
typedef struct {
    const char* name;
    const char* type_string;
    bool        is_const;
} AngaraFieldDef;

// Describes a native class with fields and methods.
typedef struct AngaraClassDef {
    const char*            name;
    const AngaraFieldDef*  fields;
    const AngaraMethodDef* methods;
} AngaraClassDef;