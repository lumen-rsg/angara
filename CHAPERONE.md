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
| 7 | **`ref<T>`** | A non-owning reference type. `data` types can hold `ref<T>` (borrow checked by the Chaperone — E509 dangling borrow) but cannot own `owned`/`class` types directly. |
| 8 | **Unified Allocator** | A single interface: `alloc` / `realloc` / `free`. Hosted: wraps the OS allocator. Freestanding: user-provided. Every allocation routes through it. |
| 9 | **Full interprocedural analysis** | The Chaperone analyzes the entire program (all modules, all functions). Derives ownership annotations automatically. |
| 10 | **Rip out the GC entirely** | ChaperoneGC, MarkSweepGC, GarbageCollector, arenas, alloc-list, safepoints, root tracking — all removed. |
| 11 | **Strings / lists / records** | Only tracked if declared `owned`. Built-in `string`, `list`, `record` are untracked (fixed-size value types for the Chaperone's purposes). `owned String` / `owned List` would be tracked. |
| 12 | **Not Rust** | No lifetimes in the type system. No borrow checker that rejects code. The Chaperone is a verifier — it analyzes any code pattern and reports exactly where it can't prove safety. |

## Architecture

```
  Angara Source (explicit `new`, `drop`, `ref<T>`, `@unsafe`)
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
- `Escaped` — ownership transferred (returned, passed to a consuming function,
  stored in an untracked container, captured by a closure). Using it is suspect.
- `Moved` — ownership moved to another variable (`let x = y` / `x = y` of a
  tracked type, or `this.f = y` in a method). Using or dropping it is an error
  (E507 use-after-move / E503 double-free).

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

`buf = Buffer(2)` when `buf` is already `Live` → the old allocation
**leaked**. Report E501. (Compound assigns `+=`/etc. keep the target's
allocation — they don't transfer ownership.)

### Move-on-assign (`unique_ptr` semantics)

A tracked allocation has **exactly one owner**. Assigning a tracked `Live`
variable to another variable **moves** ownership — the source becomes `Moved`
(invalid), the destination becomes `Live`:

```angara
let b1 = Buffer(2);
let b2 = b1;     // MOVE: b2 owns it now; b1 is Moved
b2.value = 0;    // OK — use the new owner
drop b2;         // OK — only the owner drops
b1.value = 0;    // E507 use-after-move
drop b1;         // E503 double-free (b1 was moved)
```

This is what makes the double-free guarantee real: no two `Live` names share
one allocation. `data` types are **not** moved — they copy-on-assign (Stage 7).
The same rule applies to `this.field = tracked_var` inside a method: the field
takes ownership and the source is moved. This is `unique_ptr` semantics, **not**
a borrow checker — there are no lifetimes in the type system, and no otherwise-
valid pattern is rejected; only genuinely unsound aliasing is.

### The `ref<T>` borrow check

`ref<T>` is a non-owning reference (a raw pointer at runtime) — borrowing a
tracked object without owning it. The Chaperone records the borrow
(`ref → referent`) and checks it at **use** time:

```angara
let b = Buffer(2);
let r as ref<Buffer> = b;   // r borrows b; b still owns it
r.value = 0;                 // OK — referent is live
drop b;
r.value = 0;                 // E509 dangling borrow — referent was dropped
```

A valid borrow — ref used while the referent is live, then dropped after the
ref's last use — is **not** a false positive (the check fires only when the ref
is *read* with a dead referent). This is a minimal, scope-bound check; it is
not a lifetime system.

> **v5 model:** there is **no auto-unwind**. Earlier designs inserted drop
> nodes before a `throw`; that was removed (commit `cde8e23`). Cleanup on a
> throw path is now the programmer's responsibility, verified by the Chaperone.

For every `throw` statement, the Chaperone reports **E501** for any `Live`
tracked variable that would leak when the throw fires — the programmer must
`drop` it before the throw, or structure cleanup so it doesn't leak.

**`finally {}` discharges the obligation (S8).** If the `throw` is inside a
`try` whose `finally {}` drops a variable, that variable is *not* flagged at
the throw point — the finally is guaranteed to run on the throw path, so the
allocation is released. (A `finally` that does *not* drop a variable still
leaves the throw-path E501 in force.)

```angara
let b = Buffer(1024);
try {
    if (err) throw Exception("boom");   // b is Live here
} catch (e) {
    // ...
} finally {
    drop b;   // discharges b on BOTH the normal and throw paths
}
```

**Assumption:** `drop` itself never throws (finalizers are simple free calls).
If a finalizer needs error handling, that's a future concern.

### Cycle detection (type-declaration time)

The Chaperone builds the **ownership graph** at compile time:

- For each `owned`/`class` type, its fields that are also `owned`/`class` types
  are directed edges.
- DFS for cycles.
- If a cycle is found, report E504 and require `ref<T>` for the
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
cascade needed. `ref<T>` fields are not cascaded (non-owning).

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
   during Phase 2 analysis, it looks up the callee's summary (by name; method
   calls also try the bare method name):
   - If `DROPPED`: the argument transitions to `Dropped`.
   - If `ESCAPED`: the argument transitions to `Escaped`.
   - If `BORROWED`: the argument stays `Live`.
   - If unknown (external function, FFI, or not yet analyzed): the default is
     **borrow** — the argument stays `Live`. This is correct for ~90% of FFI.

4. **Whole-program fixed point:** All functions (top-level + class methods) are
   analyzed iteratively until no summary changes (8-pass cap), so **forward
   references and (mutual) recursion converge**. Diagnostics are suppressed
   during convergence and emitted only on the final pass (no duplicates).

5. **Summary shape:** Summaries are **positional** — `summary[i]` is the
   behavior of the parameter the i-th call argument binds to (`this` is
   implicit, not a call arg). This makes multi-parameter matching correct: a
   function that drops its first tracked arg and borrows its second applies
   each behavior to the right argument.

   > **Not yet implemented:** summary serialization (separate compilation /
   > module interfaces), and `owned`/`borrow` signature annotations surfaced in
   > LSP hover/docs. Summaries are derived internally per build today.

6. **Recursion handling:** The first pass uses conservative summaries
   (`BORROWED` for all parameters). Subsequent passes refine via the fixed
   point. If the summary doesn't converge within the pass cap, the last pass's
   summaries stand (conservative).

### Escape hatches

- **`@unsafe` blocks** — the Chaperone **does analyze** `@unsafe` blocks — it
  tracks state, detects leaks/double-drops/UAF — but reports them as
  **warnings, not errors**. The state still flows through (a variable dropped
  inside `@unsafe` is `Dropped` for subsequent code outside). This gives the
  programmer feedback even in unsafe code, while allowing intentional
  rule-breaking. This is the universal opt-out.
- **`ref<T>`** — a non-owning borrow (see "The `ref<T>` borrow check" below).

> **Not yet implemented**:
> - **`@manual`** — annotation to exclude a variable from tracking. Today the
>   only opt-out is `@unsafe`.
> - **`Rc<T>` / `weak<T>`** — explicit ARC for genuinely shared ownership. Not
>   present; model shared ownership with `ref<T>` + manual lifecycle for now.
>
> **Implemented** (v5.1, 2026-07-07):
> - **`@consumes` / `@escape`** — annotations on foreign/module declarations to
>   mark that a callee frees or stores an argument. Stored on `FunctionType` so
>   they propagate across module boundaries (`attach`). At call sites, the
>   Chaperone uses the annotation to transition arguments to `Dropped` or
>   `Escaped`. Without annotations, foreign/module calls still default to
>   **borrow** (conservative).

### FFI interaction

**Foreign functions** (`foreign func`) and **native module functions**
(`attach`) are opaque — the Chaperone can't analyze their bodies. With
`@consumes`/`@escape` annotations now implemented, the rules are:

| Call kind | Argument transition | Return transition |
|---|---|---|
| foreign / module (no annotation) | **Borrow** — stays `Live` | If tracked type → new `Live` |
| foreign / module `@consumes(i)` | **Dropped** — ownership transferred to callee | If tracked type → new `Live` |
| foreign / module `@escape(i)` | **Escaped** — ownership escapes via callee | If tracked type → new `Live` |

**Module C-API signatures** (`"o->o"`, `"s?->n"`, etc.): the Chaperone reads
the type string — `o` params are tracked (default: borrow), `s`/`i`/`n`/`b`/`d`
are untracked. Use `@consumes`/`@escape` on the foreign function declaration
to mark `o` arguments that are freed or stored by the C API.

## Diagnostics

| Code | Metaphor | Technical meaning |
|---|---|---|
| **E501** | 🧬 Unfolded molecule | Memory leak — allocation has no `drop` on a control-flow path (return, throw, function exit, or reassignment of a still-live tracked var). Also fires for module-level (global) tracked allocations, which have no scope to drop them in. |
| **E502** | 💀 Dead reference | Use-after-free — variable used after `drop` (including in loop conditions, `for-in` iterables, and other recurring expressions). |
| **E503** | ⚠️ Double denaturation | Double-free — variable dropped twice, or dropped after its ownership moved/escaped. |
| **E504** | 🔗 Tangled molecule | Reference cycle — the ownership graph (tracked type → tracked fields) has a cycle. Use `ref<T>` for the back-edge. |
| **E505** | 🧬 Escaped molecule | Tracked allocation escapes into an untracked container (`list`/`record` literal) or is captured by a closure — the container/closure isn't tracked, so it can't be dropped correctly. |
| **E506** | 🔄 Incomplete fold | Loop-body drop — variable dropped in loop without reassignment, double-free on iteration 2+. |
| **E507** | 📤 Moved molecule | Use-after-move — a moved-from variable (source of `let x = y` / `x = y` of a tracked type, or `this.f = y` in a method) is read. Read the new owner instead. |
| **E509** | 🔗 Dangling borrow | A `ref<T>` is read after its referent was dropped/moved — the borrow dangles and would read freed memory. |
| **E510** | 🧵 Thread escape | A tracked allocation's ownership was transferred to another thread via `spawn()`. Using or dropping it in the parent thread is a data race / use-after-transfer / double-free. |
| **E511** | 🧵 Not sendable | A tracked type is not marked `@sendable` and cannot be transferred across thread boundaries via `spawn()`. Add `@sendable` to the type declaration. |
| **E512** | 🔒 Double lock | A `Mutex` was locked while already locked — potential deadlock. |
| **E513** | 🔓 Double unlock | A `Mutex` was unlocked without being locked — logic error. |
| **E514** | 🧵 Shared borrow | A `ref<T>` to a tracked value exists in the parent thread while the value is transferred to another thread via `spawn()`. The ref and the spawned thread can access the same memory concurrently — a data race. |
| **E515** | 🧵 Non-Sync ref | A `ref<T>` to a **non-Sync** type exists when the value is spawned. Mark the type `@sync` and add synchronization, or drop the ref before spawning. |
| **E516** | 🧵 Async escape | A tracked value that is not Send is held across an `await` point. The async function may resume on a different thread, so all tracked values must be Send. Add `@sendable` or drop before the await. |
| **W514** | (warning) | A `ref<T>` to a Sync type crosses a thread boundary — type is Sync but ensure proper synchronization (e.g., Mutex). |
| **W510** | (warning) | Variable handled differently on if/else branches. |

> **E508** is reserved (unused). **E510–E516** are M11 concurrency-safety diagnostics. **W514** is the Sync-ref-across-threads warning.
> except `W510`. Inside an `@unsafe` block, *all* codes are downgraded to
> warnings — the Chaperone still analyzes the block and reports, but does not
> enforce.

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
  Add `drop buf;` before the return, or wrap the allocation in a
  try/finally that drops it on every path.

  [E501] Leak: `Buffer` at {file:12}
```

## Staging

| Stage | Goal | Status |
|---|---|---|
| **0a** | Rip out the GC — remove ChaperoneGC, MarkSweepGC, GarbageCollector. Replace `__ang_gc_alloc` with direct malloc. | ✅ done (`209b23c`) |
| **0b–d** | Strip GC root tracking from codegen + remove `--gc` flag + gc_strategy plumbing. | ✅ done (`9556523`) |
| **1** | Add the **Allocator** interface. Replace direct malloc with allocator calls. | ✅ done (`a28cce1`) |
| **2** | Add **`owned` keyword** + **`drop` statement**. Lexer/parser/AST/type-checker/codegen. | ✅ done (`8193f51`) |
| **3** | Build the **Chaperone pass** — data-flow analysis (Phases 1–4). | ✅ done |
| **4** | **Interprocedural analysis** — Phase 5: whole-program ownership propagation (fixed-point). | ✅ done |
| **5** | **`ref<T>`** — non-owning reference type + minimal borrow check (E509). | ✅ done |
| **6** | **Drop cascades** in codegen — containers + owned fields. | ✅ done |
| **7** | **`data` value semantics** — copy-on-assign (BUG-1). | ✅ done |
| **8** | **Update stdlib / examples / tests** — explicit drops everywhere. | ✅ done (ongoing as new code is written) |

### Chaperone soundness pass (post-Stage 8, on `v5-chaperone`)

A full audit (`AUDIT-CHAPERONE.md`) found the pass detected bugs but the driver
shipped crashing binaries anyway, and several bug classes slipped through.
Resolved across six phases (all soundness holes now closed):

| Phase | Closed | Highlights |
|---|---|---|
| **0** | S0, T1, D6 | Codegen now gated on Chaperone errors (was ignored → segfaulting binaries); E504 cycle detection fixed (never fired); test suite added |
| **1** | S1, S7 | **Move-on-assign** (`unique_ptr` semantics, `Moved` state) — the core double-free guarantee; reassignment-leak detection |
| **2** | S2 | Loop conditions, `for-in` iterables, `for` increments analyzed |
| **3** | S6, G7, S9 | Field-ownership moves; class methods now analyzed; `analyzeFunction` state-discard bug fixed |
| **4** | S4, S5 | Interprocedural **fixed-point** (forward refs + recursion converge); positional summaries (multi-param) |
| **5** | G1, G2, G4, G6, S8 | Globals, closures, E505 container/closure escape, optionals, throw/`finally` discharge |
| **6** | S3 | `ref<T>` minimal borrow check (E509 dangling borrow) |

29 negative/positive tests in `tests/chaperone/` (`make test-chaperone`).

## Progress log

- **2026-06-18** — Phases 1–2 committed on `stable` (memory-safety + GC
  soundness for the OLD GC model). Phase 3 (concurrent GC) reached a dead end.
  **Decision: rip out the GC. Build the Chaperone. Angara v5.**
- **2026-06-19** — Stage 0 (rip GC) + Stage 1 (Allocator) + Stage 2 (`owned` +
  `drop`) committed on `v5-chaperone`. Stages 3–8 followed (Chaperone pass,
  interprocedural analysis, `ref<T>`, drop cascades, `data` value semantics,
  stdlib/test updates).
- **2026-06-30** — Full Chaperone audit + six-phase soundness pass on
  `v5-chaperone`: all 10 soundness holes closed (S0–S9), 6/8 coverage gaps
  resolved, 29-test Chaperone suite. See `AUDIT-CHAPERONE.md`.
