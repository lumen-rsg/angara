# The `any` Type and `@unsafe` Blocks

Dynamic typing and escape hatches.

---

## The `any` Type

`any` is the top type that can hold any Angara value:

```angara
let items as list<any> = [42, "hello", true];
```

Use `any` when you need to store values of different types in the same collection or when the type is not known at compile time.

## `@unsafe` Blocks

Calling functions through `any` typed values requires an `@unsafe` block:

```angara
@unsafe {
    result.push(transform(item));
}
```

The `@unsafe` annotation is Angara's escape hatch for dynamic behavior. It is required when:
- Calling functions passed as `any` parameters
- Invoking dynamically-dispatched methods on `any` values
- Performing operations that bypass static type checking

This ensures the programmer explicitly acknowledges the loss of static type safety at these points.

## Common Use Case: Higher-Order Functions with `any`

The standard library's `collections` module uses `@unsafe` internally to support dynamic transform and predicate functions:

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

## When to Use `@unsafe`

- Dynamic dispatch through `any` typed function values
- Interop with dynamically-typed data (JSON parsing, etc.)
- Implementing generic container operations

The `@unsafe` block scopes the unsafety to the minimum necessary code, keeping the rest of the program statically safe.
