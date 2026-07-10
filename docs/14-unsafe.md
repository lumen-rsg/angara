# Annotations and Escape Hatches

Angara provides several annotations for cases where the compiler's static analysis
cannot verify correctness — dynamic typing, manual memory management, FFI ownership,
and concurrency safety.

---

## `@unsafe` Blocks

The `@unsafe` annotation is Angara's universal escape hatch. It is required when:
- Calling functions passed as `any` parameters
- Invoking dynamically-dispatched methods on `any` values
- Performing operations that bypass static type checking
- Casting integers to pointers (and vice versa)
- Writing inline assembly (`asm(...)` — see [Bare-Metal Programming](23-bare-metal.md#inline-assembly))

```angara
@unsafe {
    result.push(transform(item));
}
```

```angara
// Inline assembly always requires @unsafe — it bypasses the type system
// and the borrow/escape analysis.
let el as i64 = 0;
@unsafe {
    asm("msr daifset, #3");
    el = asm("mrs $0, CurrentEL" -> i64, out("=r") el);
}
```

The `@unsafe` block scopes the unsafety to the minimum necessary code, keeping the
rest of the program statically safe.

### Effect on the Chaperone

The Chaperone **does analyze** `@unsafe` blocks — it tracks ownership state and
detects leaks, double-drops, and use-after-free — but reports them as **warnings,
not errors**. The ownership state still flows through: a variable dropped inside
`@unsafe` is considered `Dropped` for subsequent code outside the block.

---

## The `any` Type

`any` is the top type that can hold any Angara value:

```angara
let items as list<any> = [42, "hello", true];
```

Use `any` when you need to store values of different types in the same collection
or when the type is not known at compile time.

### Common Use Case: Higher-Order Functions

The standard library's `collections` module uses `@unsafe` internally to support
dynamic transform and predicate functions:

```angara
export func Select(items as list<any>, transform as any) -> list<any> {
    @unsafe {
        let result as list<any> = [];
        let i = 0;
        while (i < len(items)) {
            result.push(transform(items[i]));
            i = i + 1;
        }
        return result;
    }
    return [];
}
```

---

## `@manual` — Manual Memory Management

The `@manual` annotation excludes a variable declaration from Chaperone tracking.
Use it when you manage a resource's lifetime manually (e.g., GPU textures, foreign
handles, or circular data structures).

```angara
@manual let _tex = gui.nil_texture();   // GPU texture, managed manually
@manual let _handle = foreign_library.create();  // FFI handle
```

**Without `@manual`**, the Chaperone would track the allocation and require a
`drop` at the end of scope. With `@manual`, the Chaperone ignores the variable
entirely — you are responsible for its cleanup.

> **Use sparingly.** Prefer `@unsafe` blocks when you only need to suppress
> Chaperone errors temporarily. Use `@manual` only for resources whose lifetime
> is managed entirely by external code (e.g., GPU APIs, C libraries with their
> own allocators).

---

## `@consumes` and `@escape` — FFI Ownership Annotations

When calling foreign functions (`foreign func`) or native module functions
(`attach`), the Chaperone cannot analyze the callee's body. By default, all
arguments are treated as **borrowed** — the caller retains ownership.

Use `@consumes(i)` and `@escape(i)` to tell the Chaperone that the callee takes
ownership or stores the argument:

```angara
// The C function frees the buffer — we transfer ownership.
@consumes(0)
foreign func c_free_buffer(buf as *i8) -> nil;

// The C function stores the pointer — the allocation escapes.
@escape(0)
foreign func c_register_callback(cb as *void) -> nil;
```

| Annotation | Meaning | Chaperone effect |
|---|---|---|
| (none) | Borrow — callee reads but doesn't keep | Argument stays `Live` |
| `@consumes(i)` | Callee frees/destroys the argument | Argument transitions to `Dropped` |
| `@escape(i)` | Callee stores/retains the argument | Argument transitions to `Escaped` |

The `i` is a zero-based parameter index. Multiple annotations can be stacked:

```angara
@consumes(0) @escape(2)
foreign func process(a as *i8, size as i64, dest as *void) -> nil;
```

Without these annotations, foreign/module calls default to **borrow** (conservative).
This means you'll get E501 (leak) errors if the foreign function actually frees
the argument — add `@consumes` to tell the Chaperone the ownership is transferred.

---

## `@sendable` — Thread-Safe Transfer

Marks a type as safe to transfer across thread boundaries via `spawn()`.
Types without `@sendable` cannot be passed to another thread (E511).

```angara
@sendable
class WorkerState {
    let data as list<i64>;
}
```

Marking a type `@sendable` asserts that:
- The type contains no thread-local references
- All fields are themselves `@sendable`
- It is safe to move an instance to a different thread

---

## `@sync` — Thread-Safe Shared Access

Marks a type as safe for concurrent access through `ref<T>` across thread
boundaries (via `spawn()`). A `@sync` type can have `ref<T>` borrows alive
when the owning value is spawned to another thread.

```angara
@sync @sendable
class SharedCounter {
    let mutex as Mutex;
    let value as i64;
}
```

Without `@sync`, holding a `ref<T>` while spawning the owned value produces
**E515** (error) or **W514** (warning for `@sync` types). Mark `@sync` when
you have internal synchronisation (e.g., a `Mutex`) that makes concurrent
access safe.

---

## Summary Table

| Annotation | Scope | Purpose |
|---|---|---|
| `@unsafe` | Block | Suppress static checks; Chaperone errors → warnings |
| `@manual` | Variable (`let`) | Exclude variable from Chaperone tracking |
| `@consumes(i)` | Foreign/module function | Callee frees the argument (ownership transfer) |
| `@escape(i)` | Foreign/module function | Callee stores the argument (allocation escapes) |
| `@sendable` | Type declaration | Type can be transferred to another thread |
| `@sync` | Type declaration | Type is safe for concurrent `ref<T>` access |

For the full memory model and ownership system, see [CHAPERONE.md](../CHAPERONE.md).
