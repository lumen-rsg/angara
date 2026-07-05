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
