# Type Inspection with `is`

Runtime type checking with compile-time type narrowing.

---

## The `is` Expression

The `is` expression checks whether a value is of a specific type at runtime:

```angara
let items as list<any> = [42, "hello"];
for (item in items) {
    if (item is i64) {
        io.println(1, "found i64");
    }
    orif (item is string) {
        io.println(1, "found string");
    }
}
```

## Type Narrowing

Inside an `is` block, the compiler **narrows the type**, allowing direct method calls and field access without casts:

```angara
class Foo {
  public:
    func method(this) -> nil {
        io.println(1, "Foo.method() was called!");
    }
}

let l as list<any> = [Foo(), "test", 15];
for (item in l) {
    if (item is Foo) {
        item.method();       // Statically safe -- compiler knows item is Foo
    }
    orif (item is string) {
        io.println(1, item); // Compiler knows item is string
    }
    else {
        io.println(1, "something else");
    }
}
```

## Works with `orif`

Type narrowing chains correctly with `orif`:

```angara
if (item is i64) {
    // item is i64 here
}
orif (item is string) {
    // item is string here (not i64)
}
else {
    // item is neither i64 nor string
}
```

## Complete Example

```angara
attach io;

class Foo {
  public:
    func method(this) -> nil {
        io.println(1, "Foo.method() was called!");
    }
}

func main() -> i64 {
    let f = Foo();
    let l as list<any> = [f, "test", 15];

    for (item in l) {
        if (item is Foo) {
            item.method();
        }
        orif (item is string) {
            io.println(1, "Found a string: " + item);
        }
        else {
            io.println(1, "Found something else.");
        }
    }
}
```
