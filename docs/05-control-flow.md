# Control Flow

Conditional execution, loops, and flow control statements.

---

## if / orif / else

Angara uses `orif` instead of `else if`:

```angara
if (x > 50) {
    io.println(1, "big");
} orif (x > 20) {
    io.println(1, "medium");
} else {
    io.println(1, "small");
}
```

## if-let

Conditional variable binding. Binds the result of an expression and enters the block if it is truthy:

```angara
if (let result = try_parse()) {
    // result is in scope
}
```

## while

```angara
let i = 0;
while (i < 10) {
    i = i + 1;
}
```

## C-style for

```angara
for (let i = 0; i < 10; i = i + 1) {
    io.println(1, string(i));
}
```

## for-in

Iterate over collections:

```angara
let items = [10, 20, 30];
for (item in items) {
    io.println(1, string(item));
}
```

## Range-based for-in

Iterate over a numeric range without allocating a list:

```angara
for (i in 0..5) {       // exclusive: 0, 1, 2, 3, 4
    io.println(1, string(i));
}

for (i in 0...5) {      // inclusive: 0, 1, 2, 3, 4, 5
    io.println(1, string(i));
}
```

Ranges can also be materialized into lists:

```angara
let xs = 0..3;          // [0, 1, 2]
let ys = 1...4;         // [1, 2, 3, 4]
```

## break and continue

```angara
while (true) {
    if (done) { break; }
    if (skip) { continue; }
}
```

## Ternary Expression

```angara
let val = condition ? 42 : 99;
```
