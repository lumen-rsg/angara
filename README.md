# Angara 

**Angara** is a modern, statically-typed, compiled language designed for high performance, concurrency, and robust modularity. It combines the familiar C-style syntax with powerful features from languages like Rust, Python, and C++, making it an excellent choice for everything from high-level scripting to systems-level tasks.

Originally a dynamically-typed interpreted language, Angara has been completely rewritten from the ground up. It now features a sophisticated multi-pass Type Checker and a C Transpiler that generates clean, efficient, and portable C code, which is then compiled into a native executable. This architecture provides the performance of a low-level language with the high-level ergonomics of a modern scripting environment.

The name "Angara" comes from the Angara River, the only river that flows out of the incredibly deep and pure Lake Baikal. This reflects the language's philosophy: a strong, clear flow of logic from a pure and solid foundation.

## Key Features of Angara

*   **Statically Typed:** A rich, modern type system catches errors at compile time, not runtime.
*   **Compiled to Native:** Transpiles to clean C and compiles down to a native executable for maximum performance.
*   **True Multi-Threading:** Built from the ground up for true, OS-level concurrency. Features threads and mutexes for safe parallel programming.
*   **Modular by Design:** A powerful module system allows for building large, scalable applications from smaller, reusable components.
*   **Extensible C ABI:** A stable, pure C Application Binary Interface (ABI) allows developers to write high-performance native modules in C, C++, Rust, or any language that can export C symbols.
*   **Modern OOP:** A clean object-oriented system with classes, inheritance, and `private`/`public` access control, enhanced with `traits` for powerful code composition.

## Data Types

Angara provides a rich set of built-in types for modern software development.

| Category      | Types                                                                 | Description                                                                                             |
|---------------|-----------------------------------------------------------------------|---------------------------------------------------------------------------------------------------------|
| **Integers**    | `i8`, `i16`, `i32`, `i64` (default: `int`), `u8`, `u16`, `u32`, `u64` | A full range of signed and unsigned integers.                                                           |
| **Floats**      | `f32`, `f64` (default: `float`)                                       | Single and double-precision floating-point numbers.                                                       |
| **Primitives**  | `bool`, `string`, `void`                                              | The boolean type, a UTF-8 string, and the "nothing" type for functions that don't return a value.         |
| **Collections** | `list<T>`, `record<key: T, ...>`                                       | Generic, type-safe dynamic arrays and statically-defined records (structs).                               |
| **Concurrency** | `Thread`, `Mutex`                                                     | Built-in types for managing concurrent execution and protecting shared data.                                |
| **Special**     | `any`, `nil`                                                          | An "escape hatch" for dynamic typing and the type of the `nil` value.                                       |

## Examples

### Hello, World!

The entry point for any Angara executable is an `func main()`.

```angara
// hello.an
func main() -> i64 {
  print("Hello, Angara 2.0!");
  return 0;
}
```

### Variables and Control Flow

Angara uses a familiar C-style syntax. Variables are declared with `let` and constants with `const`.

```angara
func main() -> i64 {
  let message = "Hello";
  const count = 5;

  for (let i = 0; i < count; i = i + 1) {
    if (i % 2 == 0) {
      print(message, i, "is even");
    } else {
      print(message, i, "is odd");
    }
  }

  return 0;
}
```

### Object-Oriented Programming with Traits

Define reusable behavior with `trait`s and create robust objects with `class`.

```angara
trait Movable {
  func move(this, dx as i64, dy as i64) -> void;
}

export class Player inherits Entity uses Movable {
  public:
    let name as string;
    const speed as i64 = 10;
  
  private:
    let pos as record<x: i64, y: i64>;

  public:
    func init(this, name_val as string) {
      this.name = name_val;
      this.pos = {x: 0, y: 0};
    }

    func move(this, dx as i64, dy as i64) -> void {
      this.pos.x = this.pos.x + (dx * this.speed);
      this.pos.y = this.pos.y + (dy * this.speed);
    }
}
```

## Modules

Angara is designed for building large applications from smaller, reusable pieces.

### Angara Modules (`.an`)

You can `attach` other Angara files to import their public API. Only symbols marked with the `export` keyword are visible to other modules.

**`math_utils.an`**
```angara
export const PI as f64 = 3.14159;

export func square(n as f64) -> f64 {
  return n * n;
}
```

**`main.an`**
```angara
attach "math_utils.an";

func main() -> i64 {
  let r = 10.0;
  // Use the imported symbols via the module name.
  let area = math_utils.PI * math_utils.square(r);
  print(area);
  return 0;
}
```

### Native C Modules (`.so`, `.dylib`)

One of Angara's most powerful features is its stable, pure C ABI, which allows you to write high-performance modules in C (or any language that can export C symbols).

To create a native module, you simply include the `angara.h` header and implement the entry point.

**`my_native_lib.c`**
```c
#include "AngaraABI.h"
#include <time.h>

// func get_current_time() -> i64
static AngaraObject Angara_time_get_time(int arg_count, AngaraObject* args) {
    return angara_create_i64((int64_t)time(NULL));
}

// Define the public API
static const AngaraFuncDef MY_FUNCTIONS[] = {
    {"get_current_time", Angara_time_get_time, 0},
    {NULL, NULL, 0}
};

// Export the API to the Angara compiler
ANGARA_MODULE_INIT(my_native_lib) {
    *def_count = 1;
    return MY_FUNCTIONS;
}
```

**`main.an`**
```angara
// The path must point to the compiled shared library.
attach "lib/my_native_lib.so";

func main() -> i64 {
  let unix_timestamp = my_native_lib.get_current_time();
  print("Seconds since epoch:", unix_timestamp);
  return 0;
}
```

## Platform Support

The Angara compiler (`angc`) and its C transpiler are developed and tested on Linux and macOS. The generated C code is highly portable and should compile on any platform with a standard C99 compiler and the pthreads library.

*   **Linux:** Fully supported.
*   **macOS (ARM & x86_64):** Fully supported.
*   **Windows:** ComingEventually™

---

## Paths

Angara compiler (angc) requires you to properly place the runtime sources inside the filesystem.
The paths required for angc are:
1. runtime path: `/usr/src/angara/runtime/` , where angara_runtime.c and angara_runtime.h lives.
2. modules path: `/usr/src/angara/modules/`, where the standard library will live. (.an modules)
3. native .so modules path: `/lib64/angara/`, where the compiled .so modules live.

Note: this will be handled by the Angara package manager. see ``lumen-rsg/aurora`` after we finish developing our libau package management library.


## Developer Note

Angara is in somewhat early development stage. 
One should expect ABI changes (which we will try to keep very minimal in the dev stage), as well as new additions to the language. If a breaking change in the API, or, perhaps in the module contract will be committed, we will notify you all in advance.

---

lumina-labs, 2025. 
cv2 says hi!



