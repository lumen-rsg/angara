# Optionals and Null Safety

Compile-time null safety with `T?`, optional chaining, and nil coalescing.

---

## Optional Types

Use `T?` to declare that a value may be `nil`:

```angara
let result as i64? = maybe_find();
let name as string? = nil;
let user as User? = find_user(404);
```

## Nil Checks

Compare against `nil` to test for presence:

```angara
if (result != nil) {
    io.println(1, "Found: " + string(result));
}

if (name == nil) {
    io.println(1, "No name provided");
}
```

## Optional Chaining (`?.`)

Safely access members on optional values. Returns `nil` instead of crashing:

```angara
let user as User? = find_user(404);
let name = user?.name;          // nil (user was not found)
```

Works on any optional type -- if the left side is `nil`, the entire expression short-circuits to `nil`.

## Nil Coalescing (`??`)

Provide a default value when the left side is `nil`:

```angara
let display_name = user?.name ?? "Guest";
```

## Combined Pattern

Optional chaining and nil coalescing together provide a complete null-safe access pattern:

```angara
let user1 as User? = find_user(404);
let name1 = user1?.name ?? "Guest";      // "Guest" (user was nil)

let user2 as User? = find_user(101);
let name2 = user2?.name ?? "Guest";      // "Alex" (user was found)
```

## Non-Optional Values

Using `?.` on a non-optional value is allowed but has no effect -- the value is accessed normally:

```angara
let user3 = User("Bob");          // User, not User?
let name3 = user3?.name;          // "Bob" (no-op on non-optional)
```

## Complete Example

```angara
attach io;

data User {
    let name as string;
}

func find_user(id as i64) -> User? {
    if (id == 101) {
        return User("Alex");
    }
    return nil;
}

export func main() -> i64 {
    // User not found
    let user1 as User? = find_user(404);
    let name1 = user1?.name;
    io.println(1, "user1?.name = " + string(name1));             // nil

    let display1 = user1?.name ?? "Guest";
    io.println(1, "display name = " + display1);                 // Guest

    // User found
    let user2 as User? = find_user(101);
    let name2 = user2?.name;
    io.println(1, "user2?.name = " + string(name2));             // Alex

    let display2 = user2?.name ?? "Guest";
    io.println(1, "display name = " + display2);                 // Alex

    return 0;
}
```
