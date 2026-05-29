# Data Classes

Value types with auto-generated constructors and deep-copy semantics.

---

## Declaration

Data classes are declared with `data`. Fields use `let` (mutable) or `const` (immutable):

```angara
data Point {
    let x as i64;
    let y as i64;
}

data Person {
    let name as string;
    let age as i64;
}
```

## Construction

The constructor is auto-generated from the field order:

```angara
let p = Point(10, 20);
let person = Person("Alice", 30);
```

## Field Access

Use dot notation:

```angara
io.println(1, string(p.x));          // 10
io.println(1, string(p.x + p.y));    // 30
io.println(1, person.name);          // Alice
```

## Value Semantics

Assigning a data class creates a deep copy. Modifying the copy does not affect the original:

```angara
let p1 = Point(10, 20);
let p2 = p1;          // deep copy
p2.x = 99;
io.println(1, string(p1.x));    // 10 (unchanged)
io.println(1, string(p2.x));    // 99
```

## Cloning

Use `.clone()` for explicit deep copies:

```angara
let original = Point(10, 20);
let copy = original.clone();
copy.x = 500;
io.println(1, string(original.x));    // 10
io.println(1, string(copy.x));        // 500
```

### Shallow Copy Behavior

`.clone()` performs a shallow copy. Nested reference types (like classes) are still shared between the original and the clone:

```angara
let p = Point(0, 0);
let r1 = Rect(p, 100, 100);
let r2 = r1.clone();

p.x = 50;
// Both r1 and r2 see the change because 'origin' is a shared reference
io.println(1, string(r1.origin.x));   // 50
io.println(1, string(r2.origin.x));   // 50
```

## Generic Data Classes

```angara
data Box<T> {
    let value as T;
}

data Pair<K, V> {
    let key as K;
    let val as V;
}

let int_box = Box(42);
let str_box = Box("hello");
let entry = Pair("age", 30);
```

## Factory Functions

Data classes can be returned from functions:

```angara
func make_point(x as i64, y as i64) -> Point {
    return Point(x, y);
}

let p = make_point(5, 7);
```
