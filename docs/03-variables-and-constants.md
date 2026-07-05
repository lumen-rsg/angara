# Variables and Constants

How to declare and assign values in Angara.

---

## Mutable Variables

Declared with `let`. Type annotations use `as`:

```angara
let name as string = "Angara";
let count as i64 = 0;
let inferred = 42;              // Type inferred as i64
let letter = 'A';               // char literal
```

## Immutable Constants

Declared with `const`. Cannot be reassigned after initialization:

```angara
const PI as f64 = 3.14159;
const MAX_SIZE = 1024;
```

## Type Annotations

Type annotations are optional when the compiler can infer the type from the initializer. When provided, the `as` keyword separates the variable name from its type:

```angara
let x as i64 = 42;       // Explicit type
let y = 42;               // Inferred as i64
let greeting = "hello";   // Inferred as string
let flag = true;           // Inferred as bool
```
