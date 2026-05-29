# Angara Language Reference

**Version 3.1** | [Lumina Labs](https://github.com/lumen-rsg)

---

Angara is a statically-typed systems programming language that compiles to native machine code via LLVM. It combines low-level control and performance with modern language features: algebraic data types, pattern matching, a first-class C FFI, and mandatory type annotations.

The core philosophy is **explicit is better than implicit**: no hidden allocations, no implicit conversions, no surprises.

Source files use the `.an` extension.

---

## Language Guide

| Section | Description |
|---------|-------------|
| [01 - Lexical Structure](01-lexical-structure.md) | Comments, keywords, literals, and tokens |
| [02 - Type System](02-type-system.md) | Primitives, compound types, generics, user-defined types |
| [03 - Variables and Constants](03-variables-and-constants.md) | `let`, `const`, type annotations, inference |
| [04 - Functions](04-functions.md) | Declarations, generics, lambdas, closures, higher-order |
| [05 - Control Flow](05-control-flow.md) | `if`/`orif`/`else`, loops, ternary, `break`/`continue` |
| [06 - Data Classes](06-data-classes.md) | Value types, auto-constructors, deep copy, generics |
| [07 - Enums and Pattern Matching](07-enums-and-matching.md) | Algebraic data types, `match`, destructuring |
| [08 - Classes and Inheritance](08-classes.md) | Reference types, access control, `super`, overriding |
| [09 - Contracts and Traits](09-contracts-and-traits.md) | Interfaces, `signs`, `uses`, polymorphism |
| [10 - Collections](10-collections.md) | Lists, records, indexing, mutation |
| [11 - Optionals and Null Safety](11-optionals.md) | `T?`, `?.`, `??`, nil checks |
| [12 - Exception Handling](12-exceptions.md) | `try`/`catch`, `throw`, `Exception` |
| [13 - Type Inspection with `is`](13-type-inspection.md) | Runtime type checking, type narrowing |
| [14 - `any` and `@unsafe`](14-unsafe.md) | Dynamic typing, escape hatches |
| [15 - Foreign Function Interface](15-ffi.md) | C interop, pointers, structs, callbacks |
| [16 - Module System](16-modules.md) | `attach`, `export`, module types |
| [17 - Concurrency](17-concurrency.md) | `Thread`, `Mutex` |

## Reference

| Section | Description |
|---------|-------------|
| [18 - Operators](18-operators.md) | All operators and precedence table |
| [19 - Standard Library](19-stdlib.md) | 20+ modules: I/O, networking, crypto, data |
| [20 - Native Module API](20-native-modules.md) | C API for writing custom modules |
| [21 - Build System](21-build-system.md) | `.abs` files, project scaffolding |
| [22 - Compiler CLI](22-compiler-cli.md) | All commands and flags |
| [23 - Bare-Metal Programming](23-bare-metal.md) | Freestanding mode, intrinsics, kernel development |
| [24 - Editor Support](24-editor-support.md) | VS Code, Neovim, Vim, Tree-sitter |
