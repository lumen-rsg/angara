# Contracts and Traits

Interfaces for type-safe polymorphism.

---

## Contracts

Contracts define interfaces that require both **fields** and **methods**. Classes fulfill contracts using the `signs` keyword.

### Defining Contracts

```angara
contract Identifiable {
  public:
    let id as i64;
}

contract Loggable {
  public:
    func to_log_string(this) -> string;
}
```

### Signing Contracts

A class can sign multiple contracts. It must provide all required fields and implement all required methods:

```angara
class User signs Identifiable, Loggable {
  public:
    let id as i64 = 0;
    let name as string;

    func init(this, id_val as i64, name_val as string) -> nil {
        this.id = id_val;
        this.name = name_val;
    }

    func to_log_string(this) -> string {
        return "User(id: " + string(this.id) + ", name: " + this.name + ")";
    }
}
```

### Importing Contracts

Contracts can be defined in separate files and imported:

```angara
attach Identifiable, Loggable from "contracts.an";
```

Use `export` to make contracts visible to importers:

```angara
export contract Identifiable {
  public:
    let id as i64;
}
```

## Traits

Traits define interfaces that require **methods only** (no fields). Classes adopt traits with `uses`:

```angara
trait Drawable {
    func draw(this) -> nil;
}

class Circle inherits Shape uses Drawable {
    // ...
}
```

## Contract vs Trait

| Feature | Contract | Trait |
|---------|----------|-------|
| Require fields | Yes | No |
| Require methods | Yes | Yes |
| Keyword | `signs` | `uses` |
| Defined with | `contract` | `trait` |

## Complete Example

```angara
// --- contracts.an ---
export contract Identifiable {
  public:
    let id as i64;
}

export contract Loggable {
  public:
    func to_log_string(this) -> string;
}
```

```angara
// --- main.an ---
attach Identifiable, Loggable from "contracts.an";
attach io;

class User signs Identifiable, Loggable {
  public:
    let id as i64 = 0;
    let name as string;

    func init(this, id_val as i64, name_val as string) -> nil {
        this.id = id_val;
        this.name = name_val;
    }

    func to_log_string(this) -> string {
        return "User(id: " + string(this.id) + ", name: " + this.name + ")";
    }
}

export func main() -> i64 {
    let u = User(101, "Alice");
    io.println(1, "User ID: " + string(u.id));
    io.println(1, "Log string: " + u.to_log_string());
    return 0;
}
```
