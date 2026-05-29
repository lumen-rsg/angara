# Type System

Primitive types, compound types, generics, and user-defined types.

---

## Primitive Types

| Type | Description |
|------|-------------|
| `i8`, `i16`, `i32`, `i64` | Signed integers |
| `u8`, `u16`, `u32`, `u64`, `uint` | Unsigned integers |
| `f32`, `f64` | Floating-point numbers |
| `bool` | Boolean (`true` or `false`) |
| `string` | UTF-8 string |

## Compound Types

| Type | Syntax | Description |
|------|--------|-------------|
| List | `list<T>` | Dynamic array |
| Record | `{ key: Type }` | Dynamic key-value map |
| Function | `function(A, B) -> C` | First-class function type |
| Optional | `T?` | Null-safe wrapper |
| Pointer | `*i8`, `*void` | FFI pointer |
| Fixed Array | `i8[256]` | Inline C array for FFI |

## User-Defined Types

- **Data classes** -- value-semantic structs with deep copy
- **Classes** -- reference-semantic objects with inheritance
- **Enums** -- algebraic data types with associated data
- **Contracts** -- interfaces requiring fields and methods
- **Traits** -- interfaces requiring methods only

## Generics

Types and functions support generic type parameters:

```angara
data Box<T> {
    let value as T;
}

data Pair<K, V> {
    let key as K;
    let val as V;
}

func identity<T>(x as T) -> T {
    return x;
}
```

Generic types are instantiated by usage -- the compiler infers concrete types from arguments:

```angara
let int_box = Box(42);         // Box<i64>
let str_box = Box("hello");    // Box<string>
let entry = Pair("age", 30);   // Pair<string, i64>
```
