# Angara v5 — The Chaperone Memory Model

> **The GC is gone. Memory management is resolved at compile time.**
>
> The programmer allocates explicitly, drops explicitly. A compile-time pass —
> the **Chaperone** — verifies every allocation is released, catches
> use-after-free and double-free, and unwinds on exceptions. No runtime
> collector. No refcount. No pauses. Freestanding-friendly.

## Vision

Angara is a systems programming language. A tracing garbage collector is the
wrong memory model — it ruins freestanding (drivers, kernels, embedded),
introduces non-deterministic pauses, and is extraordinarily hard to make sound
under concurrency.

The **Chaperone** is a compile-time data-flow analysis pass (after the
TypeChecker) that takes its name from molecular chaperones: it **guides** the
programmer toward correct memory management and **prevents misfolding** (leaks,
dangling pointers). It does not manage memory at runtime. It verifies at
compile time.

## Design decisions (resolved)

| # | Decision | Detail |
|---|---|---|
| 1 | **`owned` keyword** | A new type-declaration keyword. Marks the type as dynamically allocated — must be explicitly `drop`ped. The Chaperone tracks `owned` types. |
| 2 | **`data` = pure value** | Copy-on-assign (memcpy). Stack/inline. No tracking, no drop. The zero-cost case. |
| 3 | **`class` = owned by default** | All `class` instances are tracked. Heap-allocated. Must be dropped. Same tracking as `owned`, plus methods/inheritance. |
| 4 | **`drop` statement** | Explicit deallocation: calls finalizer (`fin`) + frees via the Allocator. The programmer writes drops; the Chaperone verifies correctness. |
| 5 | **Drop cascades** | Dropping a container drops all elements. Dropping an object with owned fields drops each field recursively (reverse declaration order), then frees the object. |
| 6 | **Exception unwinding** | The Chaperone inserts drops for all live `owned`/`class` objects on throw paths. The programmer never writes `try/finally` for memory. |
| 7 | **`borrow<T>`** | A non-owning reference type. `data` types can hold `borrow<T>` (lifetime tracked by the Chaperone) but cannot own `owned`/`class` types directly. |
| 8 | **Unified Allocator** | A single interface: `alloc` / `realloc` / `free`. Hosted: wraps the OS allocator. Freestanding: user-provided. Every allocation routes through it. |
| 9 | **Full interprocedural analysis** | The Chaperone analyzes the entire program (all modules, all functions). Derives ownership annotations automatically. |
| 10 | **Rip out the GC entirely** | ChaperoneGC, MarkSweepGC, GarbageCollector, arenas, alloc-list, safepoints, root tracking — all removed. |
| 11 | **Strings / lists / records** | Only tracked if declared `owned`. Built-in `string`, `list`, `record` are untracked (fixed-size value types for the Chaperone's purposes). `owned String` / `owned List` would be tracked. |
| 12 | **Not Rust** | No lifetimes in the type system. No borrow checker that rejects code. The Chaperone is a verifier — it analyzes any code pattern and reports exactly where it can't prove safety. |

## Architecture

```
  Angara Source (explicit `new`, `drop`, `borrow`, `@escape`, `@manual`)
       │
       ▼
  Lexer → Parser → TypeChecker → CHAPERONE PASS → Codegen → LLVM IR
                                    │
                                    │  Phase 1: Collect tracked types
                                    │  Phase 2: Per-function data-flow analysis
                                    │  Phase 3: Exception unwinding (AST drop insertion)
                                    │  Phase 4: Cycle detection (ownership graph)
                                    │  Phase 5: Interprocedural ownership propagation
                                    │  Diagnostics (metaphor + technical)
                                    │
                                    ▼
  Codegen emits: allocator.alloc / allocator.free (no GC, no refcount)
       │
       ▼
  Runtime: the Allocator interface
    Hosted:      wraps malloc / HeapAlloc / mmap
    Freestanding: user-provided function pointer
```

## The type system

```angara
data Point { let x as i64; let y as i64; }
//   ↑ pure value: copy-on-assign, stack/inline, no drop, Chaperone ignores it

owned Buffer {
    let value as i64;
    let capacity as i64;
    func fin(this) { /* finalizer, called by `drop` before free */ }
}
//   ↑ dynamic memory: heap-allocated via Allocator, must be `drop`ped,
//     Chaperone tracks its entire lifetime

class Connection {
    // class = owned by default — Chaperone tracks every instance
    let buf as Buffer;          // owned field — drop cascades
    let port as i64;            // data field — inline, freed with parent
    func fin(this) { this.buf.close(); }
}

data View {
    let buf as borrow<Buffer>;  // borrows — doesn't own, lifetime tracked
    let offset as i64;
}
//   ↑ data with a borrow field: copied on assign (shallow — shares the Buffer),
//     but the Chaperone verifies the borrow doesn't outlive the Buffer
```

## The Chaperone Pass — complete design

### Allocation state machine

Every tracked variable (`class` instance or `owned` type) is in exactly one
state at each program point:

```
                        let buf = Buffer(...)
    Uninit ──────────────────────────────────► Live
       │                                           │  │
       │                                      drop  │  │ return / pass to function
       │                                           ▼  ▼
       │                                      Dropped  Escaped
       │                                           │  │
       │                                use after   │  │ use after
       │                                this = E502  │  │ = suspect
       │                                           │  │
       └── nil assignment / scope exit ────────────┘  ┘
```

**States:**
- `Uninit` — declared but not yet assigned a tracked value (nil).
- `Live` — holds a tracked allocation. Must be dropped or ownership-transferred
  by end of scope.
- `Dropped` — explicitly dropped via `drop`. Using it is use-after-free (E502).
- `Escaped` — ownership transferred (returned, passed to a function). Using it
  is suspect.

### The merge lattice (at if/else joins, loop headers)

```
           Live             ← most dangerous (needs action)
            │
    Dropped ≈ Escaped       ← safe (handled), but incomparable
            │
          Uninit            ← nothing to track
```

**Join rule:** `Live ∨ anything = Live`. If both states agree, that state. If
they disagree (and neither is `Live`), the result is `Dropped` (conservative —
accessing the variable after the merge is flagged, since at least one path freed
it or never initialized it).

**Path asymmetry reporting:** When a variable is `Live` on one branch and
`Dropped`/`Escaped` on the other, the Chaperone reports a diagnostic at the
merge point: *"Variable `buf` is handled differently on the two branches of the
if at line N. Add `drop buf;` to the path that's missing it."*

### Early returns

When a branch ends with `return`, `throw`, `break`, or `continue`, that path
does not participate in the merge. Only the paths that fall through to the join
point contribute to the merged state.

### Loop handling

1. **Fixed-point analysis:** The loop header state is the join of the pre-loop
   state and the post-body state. Iterate the body until convergence.

2. **Loop-body drop check:** If a variable was `Live` before the loop and
   `Dropped` inside the body without being reassigned to a new `Live`
   allocation before the back-edge → **double-free on iteration 2+**. Report.

3. **Loop-exit state:** After the loop, the state is the converged header
   state. If a variable is still `Live`, it must be dropped after the loop or
   by end of scope (otherwise: leak).

### Reassignment

`let buf = Buffer(2)` when `buf` is already `Live` → the old allocation
**leaked**. Report E501 with the old allocation's line.

### Exception unwinding

For every `throw` statement:

1. Identify all `Live` tracked variables in scope (current block + all enclosing
   blocks up to the nearest `catch`).
2. **Insert `DropStmt` nodes into the AST** before the `ThrowStmt`, in reverse
   declaration order. (If the throw is not directly in a `BlockStmt`, wrap it.)
3. Codegen processes the inserted drops normally — `finalize + free` emit before
   the `longjmp`.

For function-level unwinding (throw escapes the function entirely):
- Same mechanism — drop all `Live` tracked locals before the implicit re-throw
  at the function boundary.

**Assumption:** `drop` itself never throws (finalizers are simple free calls).
If a finalizer needs error handling, that's a future concern.

### Cycle detection (type-declaration time)

The Chaperone builds the **ownership graph** at compile time:

- For each `owned`/`class` type, its fields that are also `owned`/`class` types
  are directed edges.
- DFS for cycles.
- If a cycle is found, report E504 and require `borrow<T>` or `weak<T>` for the
  back-edge.

```
🔗 E504: Tangled molecule — reference cycle

  Type `Node` forms an ownership cycle:
    Node → Node  (via field `next`)

  Cascading drops would double-free. Use `borrow<Node>` for the
  back-reference, or allocate in a `region` block.

  [E504] Cycle: Node → Node
```

### Drop cascades

When `drop` is called on a type with owned fields, the codegen walks the type's
fields in reverse declaration order, drops each owned field recursively, then
frees the object itself.

```
drop connection
  → drop connection.buf (it's an owned Buffer)
    → free Buffer
  → free Connection
```

`data` fields (primitives, inline structs) are freed with the parent — no
cascade needed. `borrow<T>` fields are not cascaded (non-owning).

### Interprocedural analysis

**Goal:** Determine whether a function drops, escapes, or borrows each tracked
parameter, and propagate this to call sites.

**Phase 5 architecture (designed for extensibility — the core engine doesn't
change, only the escape annotations get more precise):**

1. **Function summary:** For each function, analyze the body and produce a
   summary:
   ```
   func process(buf as Buffer, conn as Connection) -> i64
     // buf:     DROPPED (function takes ownership)
     // conn:    BORROWED (function uses but doesn't drop)
     // return:  not owned (i64)
   ```

2. **Summary derivation:** The per-function data-flow analysis (Phase 2) already
   tracks what happens to each parameter:
   - If the parameter is `Dropped` at function exit → `DROPPED`.
   - If the parameter is `Escaped` (stored in a global, returned, passed to
     another function that takes ownership) → `ESCAPED`.
   - If the parameter is `Live` at function exit (function exit reports a leak
     for parameters that are `Live`) → the function has a leak bug.
   - Otherwise → `BORROWED`.

3. **Call-site propagation:** When the Chaperone encounters a function call
   during Phase 2 analysis, it looks up the callee's summary:
   - If `DROPPED`: the argument transitions to `Dropped`.
   - If `ESCAPED`: the argument transitions to `Escaped`.
   - If `BORROWED`: the argument stays `Live`.
   - If unknown (external function, FFI, or recursive cycle not yet analyzed):
     conservative → `Escaped` (the Chaperone loses tracking and warns).

4. **Whole-program fixed point:** Process all functions iteratively until no
   summary changes. Recursive functions are handled by starting with conservative
   summaries and refining.

5. **Summary persistence:** Summaries are cached and can be serialized (for
   separate compilation / module interfaces). An `owned` function signature
   annotation:
   ```
   func process(owned buf as Buffer) -> i64   // takes ownership
   func peek(borrow buf as Buffer) -> i64      // borrows
   func create() -> owned Connection            // returns ownership
   ```
   These are **derived** by the Chaperone from analyzing the body, not written
   by the programmer. They appear in LSP hover, docs, and diagnostics. For
   external/FFI functions, the programmer writes them manually (or uses
   `@escape`).

6. **Recursion handling:** For direct or mutual recursion, the first pass uses
   conservative summaries (`BORROWED` for all parameters). Subsequent passes
   refine. If the summary doesn't converge after N passes, fall back to
   conservative (and report a diagnostic: "analysis did not converge — treating
   all parameters as borrowed").

### Escape hatches

- **`@consumes`** — annotation on a foreign/module function declaration: "this
  function takes ownership of the argument and frees it." After the call, the
  argument transitions to `Dropped`. Used for `sqlite3_close`, `fclose`, etc.
- **`@escape`** — annotation on a foreign/module function declaration: "this
  function stores the pointer beyond the call." After the call, the argument
  transitions to `Escaped`. Used for `register_callback`, `add_handler`, etc.
- **(no annotation)** — the default. The function **borrows** its tracked
  arguments — uses them during the call and returns. The argument stays `Live`.
  This is correct for ~90% of FFI: `io.println`, `string()`, `io.write`, etc.
- **Foreign return** — if a foreign function returns a tracked type, the result
  is a new `Live` allocation (the function created it).
- **`@manual`** — annotation on a variable: "I'm managing this manually; don't
  track it." The Chaperone ignores the variable entirely.
- **`@unsafe`** — the Chaperone **does analyze** `@unsafe` blocks — it tracks
  state, detects leaks/double-drops/UAF — but reports them as **warnings, not
  errors**. The state still flows through (a variable dropped inside `@unsafe`
  is `Dropped` for subsequent code outside). This gives the programmer feedback
  even in unsafe code, while allowing intentional rule-breaking.
- **`Rc<T>`** — explicit ARC for genuinely shared ownership (graphs, caches).
  Refcount at runtime, but only when the programmer asks for it. `weak<T>`
  for cycle-breaking.

### FFI interaction

**Foreign functions** (`foreign func`) and **native module functions**
(`attach`) are opaque — the Chaperone can't analyze their bodies. It infers
ownership behavior from annotations and return types:

| Annotation | Argument transition | Return transition |
|---|---|---|
| (none) | **Borrow** — stays Live | If tracked type → new Live |
| `@consumes` | **Dropped** — freed by callee | If tracked type → new Live |
| `@escape` | **Escaped** — stored by callee | If tracked type → new Live |

**Module C-API signatures** (`"o->o"`, `"s?->n"`, etc.): the Chaperone reads
the type string — `o` params are tracked (default: borrow), `s`/`i`/`n`/`b`/`d`
are untracked. Module functions that consume or escape need the annotation on
the `.an` side:

```angara
// In the module's .an declaration file:
@consumes func close(db as SqliteDB) -> nil;
@escape func register_handler(cb as Buffer) -> nil;
func read(db as SqliteDB) -> string;   // borrows (default)
```

**`@unsafe` blocks**: the Chaperone analyzes the contents, tracks state
transitions (a `drop` inside `@unsafe` still transitions to `Dropped`), and
reports diagnostics at **warning** severity. The programmer can do things the
Chaperone would normally flag as errors, but still gets feedback:

```
⚠️ (in @unsafe) `buf` leaks — consider adding `drop buf;` before the
   block exits. The Chaperone will not enforce this in @unsafe context.

  [W521] Unsafe leak: `Buffer` at {file:12}
```

## Diagnostics

| Code | Metaphor | Technical meaning |
|---|---|---|
| **E501** | 🧬 Unfolded molecule | Memory leak — allocation has no `drop` on a control-flow path |
| **E502** | 💀 Dead reference | Use-after-free — variable used after `drop` |
| **E503** | ⚠️ Double denaturation | Double-free — variable dropped twice |
| **E504** | 🔗 Tangled molecule | Reference cycle — ownership graph has a cycle |
| **E505** | 🧬 Escaped molecule | Tracked allocation escapes into untracked container |
| **E506** | 🔄 Incomplete fold | Loop-body drop — variable dropped in loop, double-free on iteration 2+ |
| **W510** | (warning) | Variable handled differently on if/else branches |

### Example diagnostic

```
🧬 Unfolded molecule — memory leak detected

  `Buffer` allocated here is never released on the error-return path:

  12 │     let buf = Buffer(1024);
     │              ^^^ allocated
  13 │     if (buf.write(data) < 0) {
  14 │         return -1;
     │         ^^^^^^^^^^ ← `buf` leaks on this path
  15 │     }
  16 │     drop buf;

  The molecule has no folding path to deallocation at line 14.
  Add `drop buf;` before the return, or use `@escape` to transfer
  ownership intentionally.

  [E501] Leak: `Buffer` at {file:12}
```

## Staging

| Stage | Goal | Status |
|---|---|---|
| **0a** | Rip out the GC — remove ChaperoneGC, MarkSweepGC, GarbageCollector. Replace `__ang_gc_alloc` with direct malloc. | ✅ done (`209b23c`) |
| **0b–d** | Strip GC root tracking from codegen + remove `--gc` flag + gc_strategy plumbing. | ✅ done (`9556523`) |
| **1** | Add the **Allocator** interface. Replace direct malloc with allocator calls. | ✅ done (`a28cce1`) |
| **2** | Add **`owned` keyword** + **`drop` statement**. Lexer/parser/AST/type-checker/codegen. | ✅ done (`8193f51`) |
| **3** | Build the **Chaperone pass** — data-flow analysis (Phases 1–4). | ☐ **next** |
| **4** | **Interprocedural analysis** — Phase 5: whole-program ownership propagation. | ☐ future |
| **5** | **`borrow<T>`** — non-owning reference type. Lifetime tracking across borrows. | ☐ future |
| **6** | **Drop cascades** in codegen — containers + owned fields. | ☐ future |
| **7** | **`data` value semantics** — copy-on-assign (BUG-1). | ☐ future |
| **8** | **Update stdlib / examples / tests** — explicit drops everywhere. | ☐ future |

## Progress log

- **2026-06-18** — Phases 1–2 committed on `stable` (memory-safety + GC
  soundness for the OLD GC model). Phase 3 (concurrent GC) reached a dead end.
  **Decision: rip out the GC. Build the Chaperone. Angara v5.**
- **2026-06-19** — Stage 0 (rip GC) + Stage 1 (Allocator) + Stage 2 (`owned` +
  `drop`) committed on `v5-chaperone`. 156/156 tests pass. Programs compile and
  run with the new memory model. The Chaperone pass (Stage 3) is next.
