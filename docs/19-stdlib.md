# Standard Library

Angara ships with 20+ native modules written in C, loaded dynamically at runtime.

---

## I/O

| Module | Description |
|--------|-------------|
| `io` | Console I/O (stdin, stdout, stderr). `io.println(stream, message)` where stream 1 = stdout, 2 = stderr |
| `color` | Terminal color and styling |
| `term` | Terminal control utilities |

## Filesystem

| Module | Description |
|--------|-------------|
| `fs` | File operations (read, write, list, exists, remove) |
| `path` | Path manipulation and resolution |
| `archive` | Archive (zip/tar) support |

## Data Processing

| Module | Description |
|--------|-------------|
| `json` | JSON parsing and serialization |
| `csv` | CSV reading and writing |
| `sqlite` | SQLite database interface |
| `config` | Configuration file parsing |
| `args` | Command-line argument parsing |
| `sort` | Sorting algorithms |

## Networking

| Module | Description |
|--------|-------------|
| `http` | HTTP client and server (via libcurl) |
| `websocket` | WebSocket server (via libwebsockets) |
| `amqp` | AMQP/RabbitMQ client (via librabbitmq) |
| `mqtt` | MQTT messaging protocol |
| `rpc` | Remote procedure call support |
| `net` | Low-level networking |

## Cryptography

| Module | Description |
|--------|-------------|
| `hash` | Hashing algorithms (SHA-256, MD5, etc.) |
| `jwt` | JSON Web Token creation and verification |
| `uuid` | UUID generation (v4) |

## Mathematics

| Module | Description |
|--------|-------------|
| `math` | Mathematical functions (trig, sqrt, pow, abs, log) |
| `random` | Random number generation |

## System

| Module | Description |
|--------|-------------|
| `os` | Operating system utilities |
| `env` | Environment variable access |
| `time` | Time formatting, parsing, and sleep |
| `process` | Process management |
| `unistd` | POSIX standard utilities |
| `sys` | Low-level system access |

## Text Processing

| Module | Description |
|--------|-------------|
| `adv_string` | Advanced string operations (split, join, pad, trim, replace) |
| `encoding` | Encoding/decoding (Base64, hex) |
| `regex` | Regular expression matching |

## Collections (LINQ-style)

The `collections` module is written entirely in Angara and provides LINQ-inspired operations:

```angara
attach collections;

let items = Range(1, 10);
let evens = Where(items, func(x) { return x % 2 == 0; });
let doubled = Select(items, func(x) { return x * 2; });
let total = Sum(doubled);
let first = First(evens);
let sorted = SortBy(items, func(a, b) { return a < b; });
```

### Available Functions

**Creation**: `Empty`, `Range`, `RangeStep`, `Repeat`

**Access**: `First`, `Last`, `ElementAt`, `IndexOf`, `LastIndexOf`

**Query**: `Contains`, `Count`, `IsEmpty`, `Any`, `AnyMatch`, `All`, `CountMatch`, `FirstMatch`, `LastMatch`

**Transformation**: `Select`, `Where`, `ForEach`, `Aggregate`, `SelectWithIndex`, `FlatMap`, `MapStrings`, `MapChars`, `FilterChars`

**Set Operations**: `Distinct`, `Except`, `Intersect`, `Union`

**Ordering**: `SortBy`, `Reverse`, `Rotate`

**Slicing**: `Take`, `Skip`, `Slice`, `Chunk`, `SplitAt`

**Aggregation**: `Sum`, `Min`, `Max`, `Average`, `MinString`, `MaxString`, `Longest`, `Shortest`

**Combining**: `Concat`, `Zip`, `ZipWith`, `Flatten`

**String Operations**: `Join`, `SequenceEqual`, `ToStrings`, `JoinChars`, `SplitBy`

**Boolean**: `AnyTrue`, `AllTrue`, `CountTrue`

**Partitioning**: `Partition`, `Indices`

## Testing

| Module | Description |
|--------|-------------|
| `assert` | Assertion utilities for unit testing |

## GUI

| Module | Description |
|--------|-------------|
| `gui` | Dear ImGui + GLFW bindings for native GUI applications. Provides windows, widgets (buttons, sliders, text inputs, etc.), OpenGL texture rendering, and event handling. Used by the `binary_waterfall`, `mandelbrot`, and `life` examples. |

## Embedded

| Module | Description |
|--------|-------------|
| `arduino` | Arduino SDK bindings for embedded development |
| `unix` | Pure FFI POSIX bindings (zero-cost wrappers around system calls) |
