# Compiler CLI

All commands and flags for the `angc` compiler.

---

## Commands

```
angc                        Build the project in the current directory
angc <file.an>              Compile a single source file
angc run                    Build and run
angc check <file.an>        Lex + parse + typecheck (no codegen)
angc test [dir]             Run test suite
angc init [template]        Scaffold a new project
angc clean                  Remove build artifacts
angc publish                Build and copy to publish directory
angc modules                List installed native modules
angc fmt [-w] <files>       Format source files (-w writes in place)
angc watch                  Watch for file changes and rebuild
angc explain <code>         Show detailed help for an error or warning code
                            e.g., angc explain W003, angc explain E501
angc lsp                    Start LSP server
angc repl                   Start interactive REPL
```

## Flags

| Flag | Description |
|------|-------------|
| `-v`, `--version` | Show version information |
| `-h`, `--help` | Show help message |
| `-V`, `--verbose` | Show extra diagnostic output |
| `--target <triple>` | Cross-compile for a target triple |
| `--sysroot <path>` | Set sysroot for cross-compilation |
| `--freestanding` | Bare-metal mode (no libc) |
| `--kernel` | Kernel mode — emit a relocatable `.o` for a Linux kernel module |
| `--nostdlib` | Don't link standard libraries (libc, libm, pthread) |
| `--release` | Optimization level 2 |
| `--debug` | Debug mode (O0 + DWARF debug info) |
| `--dump-ast` | Print the Abstract Syntax Tree |
| `--dump-ir` | Emit unoptimized LLVM IR |
| `--emit-llvm` | Emit LLVM IR to stdout instead of compiling |
| `-Wall` | Enable all warnings |
| `-Werror` | Treat warnings as errors |
| `-Wno-XXX` | Suppress specific warning |
| `--error-format <text\|json>` | Diagnostic output format |

## Cross-Compilation

Use `--target` to compile for different architectures:

```sh
angc kernel.an --freestanding --target aarch64-unknown-none-elf
angc app.an --target wasm32
angc app.an --target riscv64
```

Supported targets include: `arm64`, `aarch64`, `x86_64`, `wasm32`, `riscv64`.

## Debugging

Use `--debug` for debug symbols and `--dump-ast` / `--dump-ir` for compiler diagnostics:

```sh
angc --debug app.an
angc --dump-ast app.an        # Print AST to stdout
angc --dump-ir app.an         # Print LLVM IR to stdout
```

## I/O Stream Convention

The `io` module uses stream numbers: `1` for stdout, `2` for stderr:

```angara
io.println(1, "normal output");     // stdout
io.println(2, "error message");     // stderr
```

## REPL

The interactive REPL supports JIT execution via LLVM ORC:

```sh
angc repl
```

## LSP Server

The built-in Language Server Protocol server provides IDE integration:

```sh
angc lsp
```

Features: diagnostics, completion, hover, and go-to-definition.
