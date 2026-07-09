<div align="center">

<img src="https://github.com/user-attachments/assets/7fd6448a-abdf-42ea-87f0-e9de3cc22390" alt="Angara" width="480"/>

# Angara

**A modern, statically-typed systems programming language powered by LLVM.**

Designed for clarity, safety, and pragmatic interoperability with C.

[![Continuous Build and Release](https://github.com/lumen-rsg/angara/actions/workflows/main.yml/badge.svg)](https://github.com/lumen-rsg/angara/actions/workflows/main.yml)
[![Docs](https://img.shields.io/badge/docs-Language%20Guide-blue.svg)](https://github.com/lumen-rsg/angara/wiki)
[![Version](https://img.shields.io/badge/version-5.1.0-orange.svg)](https://github.com/lumen-rsg/angara)

[Getting Started](#getting-started) ·
[Examples](#code-showcase) ·
[Modules](#module-ecosystem) ·
[Language Guide](https://github.com/lumen-rsg/angara/wiki)

</div>

---

## What is Angara?

Angara is a systems programming language that compiles to **native machine code via LLVM**. It combines the performance and low-level control of systems programming with the safety and expressiveness of modern language design: mandatory type annotations, algebraic data types, pattern matching, and a first-class C FFI.

The core philosophy is **explicit is better than implicit**: no hidden allocations, no implicit conversions, no surprises.

<table>
<tr>
<td width="25%" align="center">

**Safe by Default**

Compile-time checks eliminate null pointer exceptions, type mismatches, and missing return paths.

</td>
<td width="25%" align="center">

**LLVM-Powered**

Compiles directly to native code via LLVM. Supports cross-compilation, optimization passes, and bare-metal targets.

</td>
<td width="25%" align="center">

**Seamless C FFI**

Call any C library directly. Map C structs to Angara types. Zero-cost interop with `foreign` declarations.

</td>
<td width="25%" align="center">

**Rich Stdlib**

20+ native modules: I/O, JSON, HTTP, WebSocket, AMQP, filesystem, math, hashing, UUIDs, and more.

</td>
</tr>
</table>

---

## Feature Highlights

### Modern Type System

Angara provides a rich type system with primitives (`i64`, `f64`, `bool`, `string`), generics (`list<T>`), and compound types.

```angara
let name as string = "Angara";
let scores as list<i64> = [98, 87, 95];
let point as record = { x: 10.0, y: 20.0 };
```

### Data Classes

Value-semantic data blocks with auto-generated constructors and equality operators.

```angara
data Vec2 {
  let x as f64;
  let y as f64;
}

data Player {
  const id as i64;
  let name as string;
  let position as Vec2;
}

let p1 = Player(1, "Alex", Vec2(10.0, 20.0));
let p2 = p1;              // deep copy (value semantics)
p2.name = "Bob";
io.println(1, p1.name);   // still "Alex"
io.println(1, p1 == p2);  // false
```

### Enums & Pattern Matching

Algebraic data types with exhaustive pattern matching and destructuring.

```angara
enum WebEvent {
  PageLoad,
  KeyPress(string),
  Click({x: i64, y: i64})
}

let description = match (event) {
  case WebEvent.PageLoad:       { "Page loaded." },
  case WebEvent.KeyPress(key):  { "Key: " + key },
  case WebEvent.Click(pos):     { "Click at (" + string(pos["x"]) + ", " + string(pos["y"]) + ")" },
  case _:                       { "Unknown event." }
};
```

### Classes & Inheritance

Object-oriented programming with access control, constructor chaining via `super`, and method overriding.

```angara
class Entity {
  public:
    let name as string;
    let x as i64;
    let y as i64;

  public:
    func init(this, name as string, x as i64, y as i64) -> nil {
      this.name = name;
      this.x = x;
      this.y = y;
    }

    func describe(this) -> string {
      return this.name + " at (" + string(this.x) + ", " + string(this.y) + ")";
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
}
```

### Contracts & Traits

Define interfaces that classes can implement, ensuring type-safe polymorphism.

```angara
contract Loggable {
  public:
    func to_log_string(this) -> string;
}

contract Identifiable {
  public:
    let id as i64;
}
```

### Seamless C FFI

Call C functions and map C structs directly — no glue code required.

```angara
foreign "stdlib.h";
foreign "sys/utsname.h";

foreign data utsname {
  const sysname as string;
  const nodename as string;
  const release as string;
  const machine as string;
}

foreign func malloc(size as u64) -> c_ptr;
foreign func uname(buffer as c_ptr) -> i32;
foreign func free(ptr as c_ptr) -> nil;
```

### Optionals & Null Safety

Optional types (`T?`) ensure null safety is enforced at compile time.

```angara
let result as i64? = maybe_find_item(id);
if (result != nil) {
  io.println(1, "Found: " + string(result));
}
```

### Exceptions

Structured error handling with `try`/`catch` blocks.

```angara
try {
  let server = createServer(8080, callbacks);
  server.service();
} catch (e) {
  io.println(2, "Fatal: " + string(e));
}
```

### Concurrency

Built-in `Thread` and `Mutex` types for straightforward parallel programming.

```angara
let mutex as Mutex = Mutex();
let results as list<i64> = [];

mutex.lock();
results.push(42);
mutex.unlock();
```

### Cross-Compilation & Bare-Metal

Target any architecture LLVM supports — including freestanding/bare-metal environments.

```sh
angc kernel.an --freestanding --target aarch64-unknown-none-elf
```

```angara
intrinsic func peek32(addr as i64) -> i64;
intrinsic func poke32(addr as i64, val as i64) -> nil;
intrinsic func halt() -> nil;

func main() -> nil {
    // Bare-metal: write directly to UART on ARM64 QEMU
    while ((peek32(0x09000018) & 0x20) != 0) {}
    poke32(0x09000000, 72);  // 'H'
    halt();
}
```

---

## Code Showcase

### Hello, World!

```angara
attach io;

export func main() -> i64 {
  io.println(1, "Hello, world!");
  return 0;
}
```

### Functions & Recursion

```angara
attach io;

func factorial(n as i64) -> i64 {
  if (n <= 1) { return 1; }
  return n * factorial(n - 1);
}

export func main() -> i64 {
  io.println(1, "factorial(10) = " + string(factorial(10)));
  return 0;
}
```

### WebSocket Echo Server

```angara
attach Server, WebSocket, createServer from websocket;
attach io;

let clients as list<WebSocket> = [];
let mutex as Mutex = Mutex();

func on_connect(server as Server, client as WebSocket) -> nil {
  mutex.lock();
  clients.push(client);
  mutex.unlock();
  client.send("Welcome!");
}

func on_message(server as Server, client as WebSocket, message as string) -> nil {
  mutex.lock();
  for (c in clients) { c.send("echo: " + message); }
  mutex.unlock();
}

export func main() -> i64 {
  let server = createServer(8080, {
    "on_connect": on_connect,
    "on_message": on_message
  });
  while (true) { server.service(); }
}
```

---

## Getting Started

### Prerequisites

| Tool | macOS | Debian/Ubuntu |
|------|-------|---------------|
| Clang / C++23 compiler | `xcode-select --install` | `sudo apt install clang` |
| LLVM | `brew install llvm` | `sudo apt install llvm-dev` |
| Make | Included with Xcode CLT | `sudo apt install build-essential` |
| pkg-config | `brew install pkg-config` | `sudo apt install pkg-config` |

### Build & Install

```sh
# Clone the repository
git clone https://github.com/lumen-rsg/angara.git
cd angara

# Build the compiler (and optionally the native modules)
make            # Build the compiler only
make modules    # Build all native modules

# Install globally
make install    # Installs angc to /opt/homebrew/bin and modules to /opt/angara/modules
```

### Compile & Run

```sh
# Single-file compilation
angc hello.an
./hello

# Project build (uses .abs project file)
angc init          # Scaffold a new project
angc               # Build the project in the current directory
```

---

## Module Ecosystem

Angara ships with a rich set of native modules written in C, loaded dynamically at runtime.

| Module | Description |
|--------|-------------|
| `io` | Console I/O (stdin, stdout, stderr) |
| `fs` | File system operations (read, write, list, exists) |
| `path` | Path manipulation and resolution |
| `adv_string` | Advanced string operations (split, join, pad, trim) |
| `math` | Mathematical functions (trig, sqrt, pow, abs) |
| `json` | JSON parsing and serialization |
| `http` | HTTP client and server (via libcurl) |
| `websocket` | WebSocket server (via libwebsockets) |
| `amqp` | AMQP/RabbitMQ client (via librabbitmq) |
| `os` | Operating system utilities |
| `env` | Environment variable access |
| `time` | Time formatting, parsing, and sleep |
| `hash` | Hashing algorithms (SHA, MD5, etc.) |
| `random` | Random number generation |
| `uuid` | UUID generation |
| `encoding` | Encoding/decoding utilities (Base64, etc.) |
| `assert` | Assertion utilities for testing |
| `unistd` | POSIX standard utilities |
| `gui` | Dear ImGui windowing (windows, controls, textures) |
| `eventloop` | Asynchronous event loop (epoll, timers, channels) |

Modules are imported with `attach`:

```angara
attach io;
attach math;
attach Server, createServer from websocket;
```

### Writing Custom Modules

Angara provides a clean C API for writing native modules. Include `Angara.h`, define your exports, and compile as a shared library:

```c
#include "Angara.h"

AngaraObject my_add(int argc, AngaraObject* args) {
    int64_t a = ang_as_i64(args[0]);
    int64_t b = ang_as_i64(args[1]);
    return ang_i64(a + b);
}

static const AngaraFuncDef exports[] = {
    {"my_add", my_add, "ii->i", NULL},
    ANGARA_FUNC_END
};

ANGARA_MODULE_INIT(mymod) {
    *def_count = (sizeof(exports) / sizeof(AngaraFuncDef)) - 1;
    return exports;
}
```

---

## Compiler CLI

```
Usage:
  angc                        Build the project in the current directory (.abs file)
  angc init                   Scaffold a new Angara project
  angc <file.an>              Compile a single source file

Options:
  -v, --version               Show version information
  -h, --help                  Show this help message
  --dump-ast                  Debug: print the Abstract Syntax Tree
  --target <triple>           Cross-compile for a target triple (e.g. aarch64, wasm32)
  --sysroot <path>            Set sysroot for cross-compilation
  --freestanding              Freestanding mode (no libc, bare-metal)
  --gc <strategy>             Select GC strategy: mark-sweep (default), chaperone
  --nostdlib                  Don't link standard libraries (libc, libm, pthread)
```

---

## Project Structure

```
angara/
├── angc/                          # Compiler source code
│   ├── frontend/
│   │   ├── lexer/                 # Lexical analysis (tokenizer)
│   │   └── parser/                # Syntax analysis (AST construction)
│   ├── analyzer/
│   │   ├── table/                 # Symbol table management
│   │   ├── type_checker/          # Semantic analysis & type checking
│   │   │   ├── expr/              # Expression type checking
│   │   │   └── stmt/              # Statement type checking
│   │   └── printer/               # AST pretty-printer (--dump-ast)
│   ├── backend/
│   │   ├── llvm/                  # LLVM IR code generation
│   │   ├── build_system/          # Project build system (.abs files)
│   │   └── driver/                # Compilation pipeline driver
│   ├── includes/                  # Public headers (API, types, etc.)
│   └── src/                       # Main entry point
├── modules/                       # Native stdlib modules (C)
│   ├── io.c                       # Console I/O
│   ├── json.c                     # JSON support
│   ├── http.c                     # HTTP client/server
│   ├── websocket.c                # WebSocket server
│   ├── gui/gui.cpp                # Dear ImGui windowing (C++)
│   └── ...                        # 20+ modules
├── examples/
│   ├── ankernel/                  # Bare-metal ARM64 kernel example
│   ├── angfetch/                  # System-info tool (fastfetch clone)
│   └── binary_waterfall/          # ImGui byte-waterfall visualiser
├── tests/                         # Test suite
├── Makefile                       # Build system
└── angara.jpg                     # Project logo
```

---

## Contributing

Angara is open-source and contributions are welcome! Whether it's improving the compiler, adding new standard library modules, writing examples, or enhancing documentation — we'd love your help.

## License

Angara is distributed under the terms of the MIT license. See `LICENSE` for details.

<div align="center">

---

*Built by [Lumina Labs](https://github.com/lumen-rsg)*
