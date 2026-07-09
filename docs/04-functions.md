# Functions

Function declarations, generics, lambdas, closures, and higher-order functions.

---

## Function Declarations

All parameters require explicit type annotations with `as`. Return types use `->`:

```angara
func add(a as i64, b as i64) -> i64 {
    return a + b;
}

func greet(name as string) -> nil {
    io.println(1, "hello " + name);
}
```

The entry point of a program is `func main() -> i64` or `func main() -> nil`:

```angara
export func main() -> i64 {
    return 0;
}
```

## Generic Functions

```angara
func identity<T>(x as T) -> T {
    return x;
}
```

## Higher-Order Functions

Functions can accept other functions as parameters using the `function(...)` type:

```angara
func apply(f as function(i64) -> i64, x as i64) -> i64 {
    return f(x);
}
```

## Lambdas

Anonymous functions assigned to variables:

```angara
let double = func(x as i64) -> i64 {
    return x * 2;
};

let add = func(a as i64, b as i64) -> i64 {
    return a + b;
};

let fortytwo = func() -> i64 { return 42; };
```

## Closures

Lambdas capture variables from the enclosing scope:

```angara
let offset = 100;
let add_offset = func(x as i64) -> i64 {
    return x + offset;
};

io.println(1, string(add_offset(5)));    // 105
```

## First-Class Functions

Named functions can be used as values:

```angara
func multiply(x as i64, y as i64) -> i64 {
    return x * y;
}

let op = multiply;
io.println(1, string(op(6, 7)));         // 42
```

## Methods

Methods are functions that take `this` as their first parameter:

```angara
func describe(this) -> string {
    return this.name;
}
```

## Variadic Functions

Use `...` after the type annotation to accept a variable number of arguments:

```angara
func sum(count as i64, values as i64 ...) -> i64 {
    // values is accessible as a list
}
```

## Async Functions

Async functions enable cooperative concurrency. They return a `Future<T>` and can
suspend at `await` points, yielding control back to the caller or event loop.

### Declaration

Prefix a function with the `async` keyword:

```angara
async func fetch_data(url as string) -> string {
    let response = await http_get(url);
    return response;
}
```

An `async func` always returns `Future<T>` (where T is the declared return type),
even if you don't write `Future<...>` explicitly.

### Await

Use `await` inside an `async func` to suspend until a `Future<T>` resolves:

```angara
async func compute_chain() -> i64 {
    let a = await double(10);    // waits for double(10) to complete
    let b = await triple(a);     // then calls triple(a)
    let c = await double(b);     // then calls double(b)
    return c;
}
```

`await` can only be used inside an `async func` body. Using `await` outside of
one produces **E419**.

### Futures

The result of calling an `async func` is a `Future<T>`:

```angara
let f = compute_chain();    // f is Future<i64> — the function hasn't run yet
// ... do other work ...
let result = await f;       // Now wait for the result (only valid in async func)
```

A `Future<T>` is a tracked type — the Chaperone ensures it is properly dropped.
If you never await a Future, you must `drop` it explicitly.

### Async and the Chaperone

Values that survive across an `await` point must be `@sendable`, because the
async function may resume on a different thread. The Chaperone enforces this:

- **E516** — a non-Send tracked value is held across an `await` point.
- **E419** — `await` used outside an `async func`.

Fix E516 by adding `@sendable` to the type, dropping the value before the
`await`, or restructuring to avoid holding non-Send values across await points.

### Restrictions

- `async func` cannot be `foreign` (E417) — foreign functions are synchronous C imports.
- `async func` cannot be `intrinsic` (E418) — intrinsics are synchronous compiler builtins.
- `async` must be followed by a named function, not a lambda (E415).

### Async State Machine

The compiler transforms each `async func` into a state machine. Suspension points
(`await`) become state transitions. The state machine is allocated on the heap and
freed when the Future completes or is dropped.

For async I/O and event-loop integration, see the `io` and `eventloop` modules
in the [Standard Library](19-stdlib.md).
