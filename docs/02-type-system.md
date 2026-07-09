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
| `char` | Unicode code point (32-bit unsigned) |
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
| Borrow | `ref<T>` | Non-owning borrow reference (Chaperone-tracked) |
| Future | `Future<T>` | Async computation result (returned by `async func`) |

## User-Defined Types

- **Data classes** -- value-semantic structs with deep copy
- **Classes** -- reference-semantic objects with inheritance
- **Enums** -- algebraic data types with associated data
- **Contracts** -- interfaces requiring fields and methods
- **Traits** -- interfaces requiring methods only
- **Owned types** -- heap-allocated tracked types with ownership semantics (`owned` keyword)

## Ownership and References

Angara's Chaperone system tracks ownership of heap allocations at compile time.
Two special types support this:

- **`ref<T>`** -- a non-owning borrow. Use `&value` to create a `ref<T>` to a tracked
  variable. The Chaperone ensures the referent outlives the borrow (E509: dangling borrow).
- **`owned`** -- declares a type with ownership semantics. An `owned` type's allocations
  are tracked by the Chaperone and must be explicitly dropped.

For the full memory model, see [CHAPERONE.md](../CHAPERONE.md).

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
