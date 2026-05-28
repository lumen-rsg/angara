# Angara Programmer QoL Improvement Plan

## Context

Angara is a mature systems language with 37+ stdlib modules, enums, pattern matching, FFI, concurrency, and performance that sometimes beats C. However, the daily programmer experience has significant gaps: no debugging support (the `--debug` flag is a no-op), no IDE integration beyond basic Vim highlighting, no built-in test runner, no formatter, and no way to control warnings. This plan addresses these gaps in order of impact-to-effort ratio.

---

## Phase 1: Fix Broken Fundamentals

### 1.1 Fix `--debug` flag: emit DWARF info and run at O0

**Impact: HIGH** | **Effort: Small**

The `--debug` flag is parsed at `main.cpp:85-86` but does nothing -- the LLVM backend always runs O2 optimization (`LLVMBackend.cpp:79`) and never emits debug info. Programmers cannot use GDB/LLDB.

**Changes:**

1. **`angc/src/main.cpp`** -- Add `bool debug = false` to `CliFlags`, set it on `--debug` instead of just erasing. Thread it through to `CompilerDriver`.
2. **`angc/includes/CompilerDriver.h`** -- Add `bool m_debug = false` member + accessor `set_debug(bool)`.
3. **`angc/backend/driver/CompilerDriver.cpp:388`** -- Pass `m_debug` to `LLVMBackend` constructor.
4. **`angc/includes/LLVMBackend.h`** -- Add `bool m_debug` member, add `bool debug` parameter to constructor.
5. **`angc/backend/llvm/LLVMBackend.cpp`**:
   - Constructor: when `m_debug`, create `llvm::DIBuilder`, set `Debug Info Version` module flag. The `Token` struct already has `line`, `column`, and `file` for source locations.
   - `generate():79` -- Change `O2` to `m_debug ? O0 : O2`.
   - `codegenFunctionDecl()` -- Emit `DISubprogram` for each function.
   - Before object emission: finalize `DIBuilder`.
6. **`angc/backend/build_system/BuildSystem.cpp`** -- In linker command: add `-g` when debug, don't strip symbols.

**Verify:** `angc --debug test.an && lldb ./test` -- should see source lines, breakpoints by name, stack traces with file:line.

### 1.2 Add error codes to all error messages

**Impact: MEDIUM** | **Effort: Small**

`ErrorHandler::report()` accepts a `code` parameter (`ErrorHandler.h:16`) but no errors use it. Only warnings have codes (W001-W003).

**Scheme:** E001-E099 lexer, E100-E199 parser, E200-E399 type checker, E400-E499 codegen.

**Changes:** Find-and-replace across ~30 files in `angc/analyzer/type_checker/`, `angc/frontend/parser/`, `angc/frontend/lexer/`, `angc/backend/llvm/`. Each `error(token, msg)` becomes `error(token, msg, "EXXX")`. The `ErrorHandler` already displays `[EXXX]` when non-empty.

**Verify:** Compile a file with a type error -- should see `Error [E201]` in output.

### 1.3 Add warning control: `-Wall`, `-Werror`, `-Wno-XXX`

**Impact: MEDIUM** | **Effort: Small**

Currently 4 warnings always fire with no suppression. Essential for CI.

**Changes:**

1. **`angc/src/main.cpp`** -- Add `bool wall`, `bool werror`, `vector<string> suppressed_warnings` to `CliFlags`. Parse `-Wall`, `-Werror`, `-Wno-XXX`.
2. **`angc/includes/ErrorHandler.h`** -- Add `set<string> m_suppressed`, `bool m_warnings_as_errors = false`. Add setters.
3. **`angc/shared/ErrorHandler.cpp`** -- In `warning()`: if code is in suppressed set, return early. If `m_warnings_as_errors`, delegate to `report()`.
4. **`angc/backend/driver/CompilerDriver.cpp`** -- Configure `ErrorHandler` with warning settings from flags.

**Verify:** `angc -Werror file.an` should promote unused variable warning to error. `angc -Wno-W003 file.an` should suppress it.

### 1.4 Add `--error-format=json` for structured diagnostics

**Impact: MEDIUM** | **Effort: Small**

Enables tooling integration (editors, CI parsers, future LSP).

**Changes:**

1. **`angc/src/main.cpp`** -- Parse `--error-format=<text|json>`.
2. **`angc/includes/ErrorHandler.h`** -- Add `string m_error_format = "text"`, setter.
3. **`angc/shared/ErrorHandler.cpp`** -- In `report()`/`warning()`/`note()`: when JSON mode, output `{"severity":"error","code":"E201","message":"...","file":"main.an","line":10,"column":5}`.

**Verify:** `angc --error-format=json bad.an 2>&1 | jq .` should produce parseable JSON.

---

## Phase 2: Editor and Testing Experience

### 2.1 Tree-sitter grammar

**Impact: HIGH** | **Effort: Medium**

Unlocks VS Code, Helix, Neovim (native tree-sitter), Zed, and other modern editors. Currently only Vim syntax highlighting exists (`vim-angara/`).

**Deliverables:**
- New `tree-sitter-angara/` directory with `grammar.js`, `package.json`, `Cargo.toml`
- Source of truth: `angc/includes/Token.h` (70+ token types), `angc/frontend/parser/Parser.cpp` (grammar rules, precedence)
- Generated C parser via `tree-sitter generate`
- Queries: `highlights.scm`, `indents.scm`, `textobjects.scm`

**Verify:** `tree-sitter parse examples/hello.an` should produce a valid parse tree.

### 2.2 `angc test` built-in test runner

**Impact: HIGH** | **Effort: Medium**

Tests currently run via shell scripts (`tests/lang/run_tests.sh`). A built-in command makes testing accessible for every project.

**Changes:**

1. **`angc/src/main.cpp`** -- Add `test` command dispatch (`handle_test()`).
2. **New: `angc/backend/build_system/TestRunner.h/.cpp`** -- Discover `tests/` directory, compile and run each `.an` file, capture exit code. Support `positive/` (should succeed) and `negative/` (should fail to compile) conventions.
3. **`angc/includes/ConfigParser.h`** -- Add `test_dir` field to `ProjectConfig`.
4. Reuse the color-coded output style from `run_tests.sh`.

**Verify:** `angc test` in a project with `tests/` directory should discover and run tests with pass/fail output.

### 2.3 Multi-line error spans

**Impact: MEDIUM** | **Effort: Medium**

Current errors underline a single token. Multi-line constructs (blocks, multi-line function calls) only show the start.

**Changes:**

1. **`angc/includes/ErrorHandler.h`** -- Add overloaded `report(Token start, Token end, msg, code)`, `warning(Token start, Token end, msg, code)`.
2. **`angc/shared/ErrorHandler.cpp`** -- Implement multi-line rendering: first line underlined to end, intermediate lines fully underlined, last line underlined from column 0.
3. **`angc/includes/Token.h`** -- Optionally add `end_line`/`end_column`, or create a `Span {Token start; Token end;}` struct.

**Verify:** An error on a multi-line expression should show all affected lines underlined.

### 2.4 Global `print()` / `println()` shorthand

**Impact: HIGH** | **Effort: Small-Medium**

Every Angara program starts with `io.println(1, ...)`. The stream ID `1` for stdout is noise.

**Changes:**

1. **`angc/analyzer/type_checker/TypeChecker.cpp`** (constructor, ~line 57 where builtins are declared) -- Declare `println` and `print` as global variadic functions.
2. **`angc/backend/llvm/`** -- In call codegen: detect `println`/`print`, rewrite to call `__ang_io_println` with stdout stream ID hardcoded.
3. The runtime's `__ang_io_println` already exists in the io module.

**Verify:** `println("hello")` should work without `attach io` and without stream ID.

---

## Phase 3: Major Infrastructure

### 3.1 Language Server Protocol (LSP)

**Impact: VERY HIGH** | **Effort: Large**

The single biggest gap. No autocomplete, go-to-definition, hover, or real-time error checking. Modern language adoption depends on IDE support.

**Architecture:** Separate binary `angc-lsp` (or subcommand). Reuses existing `Lexer`, `Parser`, `TypeChecker` -- no codegen needed. JSON-RPC over stdin/stdout. Key existing code to reuse:
- `CompilerDriver::compileAngaraSource()` stages 1-3 (lex/parse/typecheck)
- `ErrorHandler` with JSON format (Phase 1.4) for diagnostics
- `SymbolTable` (`angc/includes/SymbolTable.h`) for completion
- `Type.h` type hierarchy for hover info

**Key LSP features:** `textDocument/publishDiagnostics`, `textDocument/completion`, `textDocument/hover`, `textDocument/definition`.

### 3.2 REPL (`angc repl`)

**Impact: HIGH** | **Effort: Large**

Interactive experimentation for learning, prototyping, and debugging.

**Architecture:** LLVM ORC JIT v2 for in-memory execution. Persistent module and symbol table across REPL lines. Readline/Linenoise for input.

**Key reuse:** All frontend stages work on single strings. Add `repl` command in `main.cpp`.

### 3.3 Code formatter (`angc fmt`)

**Impact: MEDIUM-HIGH** | **Effort: Medium-Large**

AST-based pretty-printer, similar to `ASTPrinter` (`angc/analyzer/printer/ASTPrinter.cpp`) but outputting formatted source instead of a tree. Add `fmt` command in `main.cpp`.

---

## Phase 4: Long-Term (Language Design)

| Feature | Impact | Notes |
|---------|--------|-------|
| Doc comments (`///`) + `angc doc` generator | Medium | Requires Lexer + AST changes |
| Package manager | High | Major standalone project |
| Generic collections (replace `list<any>`) | High | Language design change |

---

## Dependency Graph

```
1.2 (Error Codes) ──┬──> 1.3 (Warning Control)
                    └──> 1.4 (JSON Diagnostics) ──> 3.1 (LSP)

1.1 (Debug Flag)      [standalone]
2.1 (Tree-sitter)     [standalone, benefits LSP]
2.2 (Test Runner)     [standalone]
2.3 (Multi-line Span) [standalone]
2.4 (print shorthand) [standalone, benefits REPL]
3.2 (REPL)            [benefits from 2.4]
3.3 (Formatter)       [standalone]
```

## Verification

After each phase, run:
1. `make && make test-cpp` -- existing C++ tests must pass
2. `cd tests/lang && bash run_tests.sh` -- language tests must pass
3. Manual smoke test with a sample Angara program
4. For Phase 1.1 specifically: compile with `--debug`, verify LLDB shows source lines
5. For Phase 2.1: `tree-sitter test` on the grammar
6. For Phase 2.2: `angc test` in the project root

## Files Most Affected

| File | Changes |
|------|---------|
| `angc/src/main.cpp` | CLI flags, new commands (test, fmt, repl), flag threading |
| `angc/shared/ErrorHandler.cpp` + `.h` | Error codes, warning control, JSON output, multi-line spans |
| `angc/backend/llvm/LLVMBackend.cpp` + `.h` | Debug mode, O0, DIBuilder, DWARF |
| `angc/backend/driver/CompilerDriver.cpp` + `.h` | Thread debug/warning flags to all stages |
| `angc/analyzer/type_checker/TypeChecker.cpp` | print/println builtins, warning filtering |
| `angc/backend/build_system/BuildSystem.cpp` | Debug linker flags, test runner integration |
