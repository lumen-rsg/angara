# Classes and Inheritance

Reference types with methods, access control, and single inheritance.

---

## Class Declaration

Classes are declared with `class`. Fields and methods are grouped under `public:` or `private:` sections:

```angara
class Animal {
  public:
    let name as string;

  public:
    func init(this, name as string) -> nil {
        this.name = name;
    }

    func speak(this) -> string {
        return this.name + " speaks";
    }
}
```

## Construction

Classes are constructed by calling the class name as a function:

```angara
let a = Animal("Cat");
io.println(1, a.speak());           // Cat speaks
```

The `init` method is the constructor. It must take `this` as its first parameter.

## Access Control

Fields and methods can be placed under `public:` or `private:` sections:

```angara
class Entity {
  public:
    let name as string;
    let x as i64;
    let y as i64;

  private:
    let _internal_id as i64;

  public:
    func init(this, name as string, x as i64, y as i64) -> nil {
        this.name = name;
        this.x = x;
        this.y = y;
        this._internal_id = 100;
    }

    func move(this, dx as i64, dy as i64) -> nil {
        this.x = this.x + dx;
        this.y = this.y + dy;
    }
}
```

## Inheritance

Use `inherits` for single inheritance. Subclasses call the parent constructor with `super`:

```angara
class Player inherits Entity {
  public:
    let score as i64;

  public:
    func init(this, name as string) -> nil {
        super(name, 0, 0);      // Call parent constructor
        this.score = 0;
    }

    func add_score(this, points as i64) -> nil {
        this.score = this.score + points;
    }
}
```

The `super` call in the constructor must be the first statement.

### Calling Parent Methods

Use `super.method()` to call the parent's implementation:

```angara
func describe(this) -> string {
    let base = super.describe();    // Call parent method
    return base + " with score " + string(this.score);
}
```

### Inherited Members

Subclasses inherit all public fields and methods from their parent:

```angara
let p = Player("Alex");
p.move(10, -5);                        // Inherited from Entity
io.println(1, p.name);                 // Inherited field
io.println(1, string(p.score));        // Own field
```

## Method Overriding

Subclass methods with the same name as a parent method override it:

```angara
class Dog inherits Animal {
  public:
    func speak(this) -> string {
        return this.name + " barks";
    }
}

let d = Dog("Rex");
io.println(1, d.speak());           // Rex barks
```

## Complete Example

```angara
attach io;

class Entity {
  public:
    let name as string;
    let x as i64;
    let y as i64;

  private:
    let _internal_id as i64;

  public:
    func init(this, name as string, x as i64, y as i64) -> nil {
        this.name = name;
        this.x = x;
        this.y = y;
        this._internal_id = 100;
    }

    func describe(this) -> string {
        return "An entity named '" + this.name +
            "' at (" + string(this.x) + ", " + string(this.y) + ")";
    }

    func move(this, dx as i64, dy as i64) -> nil {
        this.x = this.x + dx;
        this.y = this.y + dy;
    }
}

class Player inherits Entity {
  public:
    let score as i64;

  public:
    func init(this, name as string) -> nil {
        super(name, 0, 0);
        this.score = 0;
    }

    func describe(this) -> string {
        let base = super.describe();
        return base + " with score " + string(this.score);
    }

    func add_score(this, points as i64) -> nil {
        this.score = this.score + points;
    }
}

export func main() -> i64 {
    let p = Player("Alex");

    // Inherited field
    io.println(1, "Player's name: " + p.name);

    // Inherited method
    p.move(10, -5);
    io.println(1, "Position: (" + string(p.x) + ", " + string(p.y) + ")");

    // Overridden method
    p.add_score(50);
    io.println(1, "Description: " + p.describe());

    return 0;
}
```
