# Exception Handling

Structured error handling with `try`, `catch`, and `throw`.

---

## Throwing Exceptions

Use `throw` with the built-in `Exception` type:

```angara
func risky(x as i64) -> i64 {
    if (x < 0) {
        throw Exception("negative value");
    }
    return x * 2;
}
```

## try / catch

Wrap code in `try` blocks and handle errors in `catch`:

```angara
try {
    let b = risky(-1);
    io.println(1, string(b));
} catch (e as Exception) {
    io.println(1, "caught: " + e.message);
}
```

The caught exception has a `.message` field containing the error description.

## Nested try / catch

Exception handlers can be nested. An inner `catch` prevents the exception from propagating to the outer handler:

```angara
try {
    try {
        throw Exception("inner");
    } catch (e as Exception) {
        io.println(1, "inner caught: " + e.message);
    }
} catch (e as Exception) {
    io.println(1, "outer caught (unexpected)");
}
```

## Complete Example

```angara
attach io;

func risky(x as i64) -> i64 {
    if (x < 0) {
        throw Exception("negative value");
    }
    return x * 2;
}

export func main() -> i64 {
    // Normal case (no exception)
    let a = risky(5);
    io.println(1, string(a));               // 10

    // Caught exception
    try {
        let b = risky(-1);
        io.println(1, string(b));
    } catch (e as Exception) {
        io.println(1, "caught: " + e.message);  // caught: negative value
    }

    // Nested
    try {
        try {
            throw Exception("inner");
        } catch (e2 as Exception) {
            io.println(1, "inner caught: " + e2.message);
        }
    } catch (e3 as Exception) {
        io.println(1, "outer caught (unexpected)");
    }

    return 0;
}
```
