# Collections

Lists and records -- Angara's built-in collection types.

---

## Lists

Dynamic arrays with literal syntax:

```angara
let numbers = [1, 2, 3, 4, 5];
let names = ["Alice", "Bob", "Charlie"];
let empty = [];
let matrix = [[1, 2], [3, 4]];
```

### Indexing

Lists are zero-indexed. Use subscript notation to read elements:

```angara
let l = [1, 2, 3];
io.println(1, string(l[0]));         // 1
io.println(1, string(l[4]));         // 5
io.println(1, string(matrix[0][0])); // 1 (nested)
```

### Mutation

Assign to subscripts to modify elements:

```angara
l[0] = 100;
io.println(1, string(l[0]));         // 100
```

### Built-in Operations

```angara
len(l)                   // Length of list
l.push(value)            // Append element
l.pop()                  // Remove last element
```

### Iteration

```angara
let total = 0;
for (item in items) {
    total = total + item;
}
```

### Typed Lists

Use `list<T>` for type-annotated lists:

```angara
let scores as list<i64> = [98, 87, 95];
let events as list<WebEvent> = [];
```

## Records

Dynamic key-value maps with literal syntax. Keys are strings:

```angara
let user = { "name": "Alex", "age": 30 };
```

### Access

Use subscript notation with string keys:

```angara
let name = user["name"];          // "Alex"
let age = user["age"];            // 30
```

### Mutation

Assign to subscripts to add or update fields:

```angara
user["role"] = "developer";       // Add new field
user["name"] = "Alex Smith";      // Update existing field
```

### Dynamic Keys

Keys can be variables:

```angara
let key = "role";
user[key] = "admin";
```

### Empty Records

```angara
let empty = {};
```

### Cloning Records

Use `.clone()` for an independent copy:

```angara
let r1 = { "id": 101, "status": "active" };
let r2 = r1.clone();
r1["id"] = 999;
io.println(1, string(r2["id"]));    // 101 (independent copy)
```

## Complete Example

```angara
attach io;

export func main() -> i64 {
    // Lists
    let l = [1, 2, 3, 4, 5];
    io.println(1, string(len(l)));           // 5
    l[0] = 100;

    let sum = 0;
    for (item in l) {
        sum = sum + item;
    }
    io.println(1, string(sum));              // 114

    // Records
    let user = {};
    user["name"] = "Alex";
    user["age"] = 30;
    io.println(1, "Name: " + user["name"]);  // Name: Alex

    return 0;
}
```
