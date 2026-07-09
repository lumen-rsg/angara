# Native Module API

How to write custom modules in C using the Angara extension API.

---

## Overview

Angara provides a C API for extending the language with native modules. Include `Angara.h`, define your exports, and compile as a shared library (`.dylib`/`.so`/`.dll`).

## Example Module

```c
#include "Angara.h"

AngaraObject my_add(int argc, AngaraObject* args) {
    int64_t a = ang_as_i64(args[0]);
    int64_t b = ang_as_i64(args[1]);
    return ang_i64(a + b);
}

static const AngaraFuncDef exports[] = {
    {"my_add", my_add, "ii->i", NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(mymod) {
    *def_count = (sizeof(exports) / sizeof(AngaraFuncDef)) - 1;
    return exports;
}
```

## Type Signature DSL

The export table uses a compact DSL for type signatures:

| Code | Type |
|------|------|
| `i` | `i64` |
| `f` | `f64` |
| `s` | `string` |
| `b` | `bool` |
| `n` | `nil` |
| `o` | `any` (object) |
| `p` | `pointer` |

Parameters are listed left of `->`, return type to the right:

| Signature | Meaning |
|-----------|---------|
| `"ii->i"` | `(i64, i64) -> i64` |
| `"is->n"` | `(i64, string) -> nil` |
| `"o->b"` | `(any) -> bool` |
| `"->s"` | `() -> string` |

## Value API

### Constructors

Create Angara values from C:

```c
ang_nil()              // nil
ang_bool(true)         // boolean
ang_i64(42)            // integer
ang_f64(3.14)          // float
```

### Type Checks

Test the type of an `AngaraObject`:

```c
ang_is_nil(obj)        // Is it nil?
ang_is_bool(obj)       // Is it a bool?
ang_is_i64(obj)        // Is it an i64?
ang_is_f64(obj)        // Is it an f64?
```

### Value Extraction

Extract C values from an `AngaraObject`:

```c
ang_as_bool(obj)       // bool
ang_as_i64(obj)        // int64_t
ang_as_f64(obj)        // double
```

## AngaraObject

The core value type is a 16-byte tagged union:

```c
typedef struct {
    int32_t tag;       // Type tag (ANG_TAG_NIL, ANG_TAG_BOOL, ANG_TAG_I64, etc.)
    int64_t payload;   // Inline value or pointer to heap object
} AngaraObject;
```

Inline values (nil, bool, i64, f64) are stored directly in the struct. Heap objects (string, list, record, etc.) store a pointer in the payload field.

### Heap Object Tags

String, list, record, exception, thread, mutex, closure, class, instance, native_instance, data_instance, enum_instance, bound_method.

## AngaraAPI Vtable

At module initialization, the runtime provides an `AngaraAPI` vtable with functions for:

- String operations (create, concat, length, subscript)
- List operations (create, push, pop, length, subscript)
- Record operations (create, get, set, keys)
- Native instances (create, field access, method calls)
- Memory management (allocate, free)
- Conversions (to_string, to_number)
- Truthiness and equality checks
- Error reporting
- Type introspection

## Module Registration

Use the `ANGARA_MODULE_INIT(name)` macro to define the module entry point. The runtime calls this function when the module is loaded:

```c
ANGARA_MODULE_INIT(mymod) {
    *def_count = (sizeof(exports) / sizeof(AngaraFuncDef)) - 1;
    return exports;
}
```

## Native Classes

Modules can also export native classes with fields and methods:

```c
static const AngaraFieldDef my_fields[] = {
    {"x", ANG_FIELD_I64, NULL},
    {"y", ANG_FIELD_I64, NULL},
    ANGARA_FIELD_END
};

static const AngaraMethodDef my_methods[] = {
    {"describe", my_describe_method, "->s", NULL},
    ANGARA_METHOD_END
};

static const AngaraClassDef my_class = {
    "MyClass", my_fields, my_methods
};
```

## C++ Modules (e.g. `gui`)

Native modules can be written in C++ when the backing library is C++ (e.g. Dear ImGui). Include `Angara.h` inside `extern "C" {}` — the header is already `extern "C"`-guarded, so you can include it directly from a `.cpp` file. Store `ang_api` as a `static const AngaraAPI*` (declared by `Angara.h`).

The **gui module** (`modules/gui/gui.cpp`) is a concrete example: it links Dear ImGui + GLFW (built from vendored sources) into a C++ shared library. Key patterns it demonstrates:

- **One-time global initialisation** — `ImGui::CreateContext()` and `glfwInit()` are called inside the first `Window` constructor, guarded by static flags.
- **Per-frame callback loop** — `Window.run(frame_fn)` mirrors `eventloop.Loop.run()`: `api->incref(frame_fn)`, a `while(!glfwWindowShouldClose)` poll, `api->call(frame_fn, 1, winArg)` each frame, then `decref` on exit.
- **Native-instance state** — `WindowData { GLFWwindow* win; }` and `TextureData { GLuint tex; int w, h; }` are stored via `api->native_instance_new` with `finalize_*` callbacks that decrement GL resources.
- **Immediate-mode widgets** — `gui.button`, `gui.text`, `gui.slider_float`, etc. are plain module-level functions that call ImGui inside the frame callback.

Usage from Angara:

```angara
attach gui;

func on_frame(_) -> nil {
    if (gui.button("Quit")) { return nil; }
    gui.text("Hello from ImGui!");
}

export func main() -> i64 {
    let win = gui.Window("Hello", 800, 600);
    win.run(on_frame);
    return 0;
}
```

See `examples/binary_waterfall/` for a complete demo.
