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
under concurrency (the Phase 3 GC effort proved this).

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
| 3 | **`drop` statement** | Explicit deallocation: calls finalizer (`fin`) + frees via the Allocator. The programmer writes drops; the Chaperone verifies correctness. |
| 4 | **Drop cascades** | Dropping a container drops all elements. No dead weight. |
| 5 | **Exception unwinding** | The Chaperone inserts drops for all live `owned` objects on throw paths. The programmer never writes `try/finally` for memory. |
| 6 | **`borrow<T>`** | A non-owning reference type. `data` types can hold `borrow<T>` (lifetime tracked by the Chaperone) but cannot own `owned` types directly. |
| 7 | **Unified Allocator** | A single interface: `alloc` / `realloc` / `free`. Hosted: wraps the OS allocator. Freestanding: user-provided. Every allocation routes through it. |
| 8 | **Full interprocedural analysis** | The Chaperone analyzes the entire program (all modules, all functions). Derives ownership annotations automatically. |
| 9 | **Rip out the GC entirely** | ChaperoneGC, MarkSweepGC, GarbageCollector, arenas, alloc-list, safepoints, root tracking — all removed. |

## Architecture

```
  Angara Source (explicit `new`, `drop`, `borrow`, `@escape`, `@manual`)
       │
       ▼
  Lexer → Parser → TypeChecker → CHAPERONE PASS → Codegen → LLVM IR
                                    │
                                    │  • CFG + data-flow analysis
                                    │  • Leak / UAF / double-free detection
                                    │  • Exception-path drop insertion
                                    │  • Interprocedural ownership inference
                                    │  • Diagnostics (metaphor + technical)
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
    let data as ptr;
    let capacity as i64;
    let length as i64;
    func init(this, cap as i64) { ... }
    func fin(this) { /* finalizer, called by `drop` before free */ }
}
//   ↑ dynamic memory: heap-allocated via Allocator, must be `drop`ped,
//     Chaperone tracks its entire lifetime

data View {
    let buf as borrow<Buffer>;    // borrows — doesn't own, lifetime tracked
    let offset as i64;
}
//   ↑ data with a borrow field: copied on assign (shallow — shares the Buffer),
//     but the Chaperone verifies the borrow doesn't outlive the Buffer
```

## Diagnostics style (metaphor + technical)

```
🧬 Unfolded molecule — memory leak detected

  `Buffer` allocated here has no `drop` on the error-return path:

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

```
💀 Dead reference — use after free

  `buf` was dropped at line 16 but accessed at line 18:

  16 │     drop buf;
     │          ^^^ dropped here
  17 │
  18 │     io.println(1, string(buf.length));
     │                    ^^^ used after drop

  The molecule has already been released. This access is invalid.

  [E502] Use-after-free: `buf` at {file:18}
```

```
⚠️ Double denaturation — double free

  `conn` is dropped a second time:

  20 │     drop conn;
  21 │     drop conn;
     │          ^^^ already dropped at line 20

  Each molecule can only be released once.

  [E503] Double-free: `conn` at {file:21}
```

## Staging

| Stage | Goal | Status |
|---|---|---|
| **0** | Rip out the GC — remove ChaperoneGC, MarkSweepGC, GarbageCollector, arenas, alloc-list, safepoints, root tracking. Replace `__ang_gc_alloc` with direct malloc. | ☐ pending |
| **1** | Add the **Allocator** interface. Replace direct malloc with allocator calls. Default = malloc wrapper. Freestanding = user-provided. | ☐ pending |
| **2** | Add **`owned` keyword** + **`drop` statement**. Lexer/parser/AST/type-checker/codegen. `owned` types allocate via Allocator; `drop` emits free + finalizer. | ☐ pending |
| **3** | Build the **Chaperone pass** — data-flow analysis. Start with leak detection, then double-free, then UAF. Iterate on diagnostics. | ☐ pending |
| **4** | **Exception unwinding** — Chaperone inserts drops on throw paths. CFG edges for try/catch/spawn. | ☐ pending |
| **5** | **`borrow<T>`** — non-owning reference type. Lifetime tracking across borrows. `data` can hold borrows. | ☐ pending |
| **6** | **Drop cascades** — dropping containers drops elements. Codegen emits element-drop loops. | ☐ pending |
| **7** | **Interprocedural analysis** — whole-program ownership inference. Function signature annotations derived automatically. | ☐ pending |
| **8** | **`data` value semantics** — copy-on-assign (BUG-1). Memcpy for data types. | ☐ pending |
| **9** | **Update stdlib / examples / tests** — explicit drops everywhere. Module C-API changes (no GC hooks). | ☐ pending |

## What gets removed (Stage 0)

- `angc/includes/GarbageCollector.h` (base class)
- `angc/includes/ChaperoneGC.h` + `angc/backend/llvm/rt/ChaperoneGC.cpp` (~2600 lines)
- `angc/includes/MarkSweepGC.h` + `angc/backend/llvm/rt/MarkSweepGC.cpp` (~1700 lines)
- `angc/includes/ChaperoneSTWGC.h` + `angc/backend/llvm/rt/ChaperoneSTWGC.cpp`
- GC runtime functions: `__ang_gc_alloc`, `__ang_gc_collect`, `__ang_gc_sweep`, `__ang_gc_mark*`, `__ang_gc_finalize`, `__ang_gc_safepoint`, `__ang_gc_push_frame`, `__ang_gc_pop_frame`, `__ang_gc_thread_register/unregister`, `__ang_gc_pin/unpin`, `__ang_gc_clear_unique`, etc.
- GC root tracking: `GcRootFrame`, `allocLocal`'s frame registration, `emitGcPushFrame`/`emitGcPopFrame`
- Safepoint polls: loop back-edge + function entry polls
- Arena system, alloc-list, free-list
- `--gc` flag + `gc_strategy` selection
- `m_gc` member in RuntimeBuilder

## What stays

- Lexer, parser, type checker (produces the typed AST the Chaperone consumes)
- Codegen (emits LLVM IR — minus GC root tracking / safepoints)
- FFI / module system (C-API minus GC hooks)
- ObjHeader (repurposed: type tag + maybe a "managed" flag)
- Diagnostics infrastructure (codes, spans, carets)
- LSP, formatter, tree-sitter grammar

## Open questions

| # | Question | Status |
|---|---|---|
| 1 | `class` vs `owned`: is `class` `owned` by default? Or is `owned` a separate, orthogonal modifier? | ☐ discuss |
| 2 | `borrow<T>` syntax: keyword (`borrow<Buffer>`) vs sigil (`&Buffer` / `*Buffer`)? | ☐ discuss |
| 3 | String type: `data` (value, borrowed buffer) vs `owned` (heap, must drop) vs interning? | ☐ discuss |
| 4 | Module C-API: how do native modules allocate/free? Direct allocator access? | ☐ discuss |
| 5 | `Rc<T>` for explicit shared ownership (ARC): syntax, semantics, cycle handling? | ☐ discuss |

## Progress log

- **2026-06-18** — Phases 1–2 committed on `stable` (memory-safety + GC soundness
  for the OLD GC model). Phase 3 (concurrent GC) reached a dead end: the GC is
  fundamentally unsound under concurrency (alloc-path races, handshake deadlocks,
  heap corruption). **Decision: rip out the GC entirely. Build the Chaperone
  compile-time pass. Angara v5.**
