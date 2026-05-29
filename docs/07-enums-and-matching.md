# Enums and Pattern Matching

Algebraic data types with associated data and exhaustive destructuring.

---

## Simple Enums

Enums without associated data:

```angara
enum Color {
    Red,
    Green,
    Blue
}

enum Direction {
    North,
    South,
    East,
    West
}
```

Use dot notation to access variants:

```angara
let c = Color.Green;
if (c == Color.Red) {
    io.println(1, "red");
} orif (c == Color.Green) {
    io.println(1, "green");
}
```

## Enums with Associated Data

Variants can carry data, making them algebraic data types:

```angara
enum WebEvent {
    PageLoad,                         // No data
    KeyPress(string),                 // Single value
    Click({x: i64, y: i64})          // Record payload
}
```

Constructing variants with data:

```angara
let event1 = WebEvent.PageLoad;
let event2 = WebEvent.KeyPress("h");
let event3 = WebEvent.Click({x: 100, y: 250});
```

## Enums in Collections

Enums can be stored in typed lists and passed to functions:

```angara
let events as list<WebEvent> = [
    WebEvent.KeyPress("H"),
    WebEvent.Click({x: 1024, y: 768}),
    WebEvent.PageLoad
];

func process_event(event as WebEvent) -> nil {
    // handle event
}

process_event(events[1]);
```

## Pattern Matching

The `match` expression destructures enum variants. Each `case` binds the payload to a variable:

```angara
let description as string = match (event) {
    case WebEvent.PageLoad:      { "Page loaded." },
    case WebEvent.KeyPress(key): { "Key: " + key },
    case WebEvent.Click(pos):    {
        "Click at (" + string(pos["x"]) + ", " + string(pos["y"]) + ")"
    },
    case _:                      { "Unknown event." }
};
```

### Wildcard Pattern

The `_` wildcard matches any unhandled variant:

```angara
let result = match (first_event) {
    case WebEvent.PageLoad: { "It was a page load." },
    case _:                 { "It was some other event." }
};
```

## Complete Example

```angara
attach io;

enum WebEvent {
    PageLoad,
    KeyPress(string),
    Click({x: i64, y: i64})
}

export func main() -> i64 {
    let events as list<WebEvent> = [
        WebEvent.KeyPress("H"),
        WebEvent.Click({x: 1024, y: 768}),
        WebEvent.PageLoad
    ];

    for (event in events) {
        let description as string = match (event) {
            case WebEvent.PageLoad:      { "PageLoad: A simple event with no data." },
            case WebEvent.KeyPress(key): { "KeyPress: The user pressed '" + key + "'." },
            case WebEvent.Click(pos):    {
                "Click: at (" + string(pos["x"]) + ", " + string(pos["y"]) + ")."
            },
            case _: { "unknown event." }
        };
        io.println(1, "  - " + description);
    }

    return 0;
}
```
