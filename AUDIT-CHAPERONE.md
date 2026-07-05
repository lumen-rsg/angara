# Chaperone Audit — Complete Memory-Leak Mitigation

> Full re-audit of the **Chaperone** compile-time memory pass and the runtime
> memory model, performed **2026-07-05**.
>
> Scope: `angc/backend/chaperone/` (Chaperone.cpp, StmtAnalysis.cpp,
> ExprAnalysis.cpp), `angc/backend/llvm/rt/` (all runtime allocators),
> `angc/backend/llvm/stmt/StmtCodegen.cpp` (cgDrop), `angc/backend/llvm/expr/ExprCodegen.cpp`
> (assignment, construction), `angc/includes/Chaperone.h`, `CHAPERONE.md`,
> the test suite.
>
> **Status legend:**
> - 🟢 **Source** — confirmed by reading the code (file:line cited).
> - ⚫ **Verified** — reproduced by compiling/running a program.
>
> Each item has a stable **ID** (`CHAP-2-XXX`) for referencing in commits/PRs.
> Check `[ ]` → `[x]` when resolved.

---

## Executive Summary

The Chaperone is a compile-time verifier that prevents use-after-free,
double-free, and leaks for `class` and `owned data` types. It works correctly
for those types within its scope. **However, it is completely blind to the
13 heap-allocated built-in types** (`string`, `list`, `record`, `exception`,
`closure`, `bound_method`, `trait_object`, `raw_array`, `vector`, `thread`,
`mutex`, `native_instance`, and non-owned `data`). Every allocation of these
types is a permanent memory leak — there is no free path, no finalizer, no GC,
and the Chaperone never flags them.

Additionally, four critical control-flow gaps let memory bugs slip past
the Chaperone's analysis undetected, and the interprocedural summary system
has a method-name collision bug that silently produces incorrect results.

**This audit replaces the 2026-06-30 audit.** All prior findings (S0–S9,
G1–G7, D1–D6, T1, L1) remain resolved. This audit focuses on the
**next frontier**: complete memory-leak mitigation across all types.

---

## Part 1: The Untracked-Allocation Crisis

### 1.1 Root Cause

The Chaperone's `isTrackedTypeObj` (`Chaperone.cpp:74-87`) returns `true` only
for `CLASS`, `INSTANCE`, and owned `DATA` types. Every other `TypeKind` returns
`false`. Untracked types are never transitioned to the `Live` state — they
remain `Uninit` forever. The E501 leak detector only scans for variables in
`Live` state, so untracked allocations are invisible.

Meanwhile, at the runtime level, `__ang_gc_alloc` is just `malloc`
(`Memory.cpp:109-132`). There is no GC, no reference counting, no cycle
detection. `__ang_gc_finalize` is a no-op (`Memory.cpp:196-200`). Objects are
freed only when `drop` or `__ang_gc_free` is explicitly called — and `drop` is
restricted to class/owned-data types by the Chaperone (E503 on others).

**Result: Every allocation that isn't a class or owned data is an unconditional,
silent, permanent memory leak.**

### 1.2 Complete Leak Inventory

| ID | Type | Subtype Tag | What Allocates | What Leaks | Severity |
|----|------|-------------|---------------|------------|----------|
| **L1** | `string` | OBJ_STRING=0 | `__ang_string_from_c` (`Strings.cpp:52-54`) | `AngaraString` struct + `strdup`'d chars buffer | **CRITICAL** — every string |
| **L2** | `string` concat (copy path) | OBJ_STRING=0 | `__ang_string_concat` copy branch (`Strings.cpp:222-246`) | Old string struct + new chars buffer (if no in-place reuse) | **CRITICAL** |
| **L3** | `list` | OBJ_LIST=1 | `__ang_list_new` (`Collections.cpp:37-38`) | `AngaraList` struct + element buffer (`malloc`'d, `Collections.cpp:150`) | **CRITICAL** — every list |
| **L4** | `record` | OBJ_RECORD=2 | `__ang_record_new` (`Collections.cpp:638-639`) | `AngaraRecord` struct + entry buffer + every key (`strdup`'d) | **CRITICAL** — every record |
| **L5** | `exception` | OBJ_EXCEPTION=3 | `__ang_exception_new` (`ControlFlow.cpp:210`) | `AngaraException` struct | **CRITICAL** — every throw/catch cycle |
| **L6** | `closure` | OBJ_CLOSURE=6 | `__ang_closure_new` (`ControlFlow.cpp:45`) | `AngaraClosure` struct + env arrays | **CRITICAL** — every closure |
| **L7** | `bound_method` | OBJ_BOUND_METHOD=12 | `__ang_bound_method_new` (`ControlFlow.cpp:143`) | `AngaraBoundMethod` struct | HIGH |
| **L8** | `trait_object` | OBJ_TRAIT_OBJECT=13 | `__ang_trait_object_new` (`ControlFlow.cpp:169`) | `AngaraTraitObject` struct | HIGH |
| **L9** | `raw_array` | OBJ_RAW_ARRAY=14 | `__ang_raw_array_new` (`Collections.cpp:456-457`) | `AngaraRawArray` struct + element buffer | **CRITICAL** |
| **L10** | `vector` | OBJ_VECTOR=15 | `__ang_vector_new` (`Collections.cpp:588`) | `AngaraVector` struct (overallocated inline buffer) | HIGH |
| **L11** | `thread` | OBJ_THREAD=4 | raw `malloc` (`ControlFlow.cpp:412`) | `AngaraThread` struct — trampoline frees args+gc_state but NOT the struct itself | HIGH |
| **L12** | `mutex` | OBJ_MUTEX=5 | raw `malloc` (`ControlFlow.cpp:477`) | `AngaraMutex` struct — no destroy/free path; `pthread_mutex_destroy` declared but never called | HIGH |
| **L13** | `native_instance` | OBJ_NATIVE_INSTANCE=9 | `__ang_native_instance_new` (`ModuleAPI.cpp:198`) | `AngaraNativeInstance` struct — no auto-free | MEDIUM |
| **L14** | **class field sub-buffers** | — | cascade drop in `cgDrop` | When a class has a `string`/`list`/`record` field, dropping the class frees the class struct but NOT the field's sub-buffers (strdup'd chars, list element arrays, record entries) because `__ang_gc_finalize` is a no-op | **CRITICAL** |
| **L15** | **nested owned fields** | — | cascade drop in `cgDrop` | When a tracked field is itself a class/owned-data with its own built-in fields, only the outer struct is freed — the cascade only calls `__ang_gc_finalize` (no-op) + `__ang_gc_free` on each tracked field, without recursively walking that field's own sub-fields for built-in buffers | **CRITICAL** |

### 1.3 Why `drop` Doesn't Help

Even if the Chaperone allowed `drop` on built-in types (it doesn't — E503),
the `cgDrop` codegen (`StmtCodegen.cpp:484-544`) would still not clean up
correctly:

- It extracts the heap pointer and walks class/data fields for cascade.
- For non-class, non-data types, it skips cascade entirely.
- It calls `__ang_gc_finalize` (no-op) + `__ang_gc_free` (frees the outer struct).
- **For a string**: the `AngaraString` struct is freed, but the `strdup`'d
  chars buffer leaks.
- **For a list**: the `AngaraList` struct is freed, but the element buffer
  (`malloc`'d separately) leaks.
- **For a record**: the struct is freed, but the entry buffer and all
  `strdup`'d keys leak.
- **For a vector**: the struct (which includes the overallocated buffer) IS
  freed correctly by `__ang_gc_free` — the inline allocation means `free`
  reclaims the whole thing. Vectors are the one built-in type that would
  actually be freed properly by a hypothetical `drop`.

### 1.4 The GC Cleanup Gap

The old GC's `__ang_gc_finalize` used to walk the object graph recursively,
freeing interior pointers before freeing the struct. This was removed in
Stage 0a (commit `209b23c`) and replaced with a no-op. The codegen still
emits calls to `__ang_gc_finalize` before `__ang_gc_free`, but the function
does nothing. This means:

- No built-in-type sub-buffers are freed.
- No nested owned fields have their sub-buffers freed.
- The only thing `drop` actually frees is the outer struct memory.

---

## Part 2: Control-Flow Analysis Gaps

### CRITICAL — Silent false negatives (memory bugs pass undetected)

| ID | Status | File:Line | Description |
|----|--------|-----------|-------------|
| **C1** | [x] 🟢 Source → ✅ Fixed | `ExprAnalysis.cpp` (no handler) | **RangeExpr not analyzed.** `a..b` expressions are not walked by `analyzeExpr`. A dropped/moved tracked variable in a range goes undetected (use-after-free silent). `collectExprVarRefs` DOES handle it (line 39), so closure capture works, but the memory-safety walk doesn't. |
| **C2** | [x] 🟢 Source → ✅ Fixed | `ExprAnalysis.cpp` (no handler) | **InterpStringExpr not analyzed.** `"Hello, {name}"` interpolations are not walked. A dropped/moved tracked variable in an interpolation segment goes undetected. `collectExprVarRefs` handles it (lines 40-43), but `analyzeExpr` doesn't. |
| **C3** | [x] 🟢 Source → ✅ Fixed | `StmtAnalysis.cpp:108` | **Method summary name collision.** Summaries are keyed by bare function name: `ctx.summaries[func.name.lexeme] = ...`. If two classes both define `init()`, the last one analyzed overwrites the first. All call sites for any class's `init` use the overwritten summary. A method that drops its first arg can have its summary replaced by one that borrows it, causing silent false negatives. |
| **C4** | [x] 🟢 Source → ✅ Fixed | `ExprAnalysis.cpp:368-384` | **Closure bodies never analyzed.** The `LambdaExpr` handler marks captures as Escaped (E505) but NEVER walks the lambda body's statements. All E501/E502/E503/E506/E507/E509 checks are missing inside closures. This is the single largest analysis gap. |

### HIGH — Potentially unsound under specific conditions

| ID | Status | File:Line | Description |
|----|--------|-----------|-------------|
| **H1** | [x] 🟢 Source → ✅ Fixed | `Chaperone.cpp:108-153` | **TryStmt missing from collectVarRefs.** Closures defined inside try/catch/finally have invisible captures — E505 never fires for variables they reference. |
| **H2** | [x] 🟢 Source → ✅ Fixed | `ExprAnalysis.cpp:268-272` | **No summary for closure calls.** When a closure is called as `f()`, there is no entry in `ctx.summaries` (closures are LambdaExprs, not FuncStmts). All args are treated as Borrowed — a closure that drops or escapes its argument goes undetected. Fixed by building summaries from lambda body analysis (keyed by FunctionType pointer) and looking them up at call sites via the callee VarExpr's type. |
| **H3** | [x] 🟢 Source → ✅ Fixed | `ExprAnalysis.cpp:272` | **@consumes/@escape implemented.** Parser recognizes `@consumes(i, j)` and `@escape(i, j)` annotations. Summary building uses annotations (takes precedence over inference). Annotated foreign functions have their summaries pre-built before the fixed-point loop. |
| **H4** | [x] 🟢 Source → ✅ Fixed | `Chaperone.cpp:308-313` | **No oscillation detection in fixed-point.** The convergence check is exact map equality. If summaries oscillate (A→B→A→B), the loop exhausts all 8 passes without converging and silently uses the last pass. No diagnostic emitted. With C3 (name collision), oscillation is plausible. |
| **H5** | [x] 🟢 Source → ✅ Fixed | `StmtAnalysis.cpp:69-118` | **Borrow tracking is intraprocedural only.** `ctx.borrows.clear()` at function entry. If a `ref<T>` is passed to another function, the callee doesn't see the borrow relationship and won't flag E509 if it drops the referent. Fixed by seeding `ctx.borrows` in `analyzeFunction`: when a function receives both a `ref<T>` param and a tracked `T` param of matching type, a borrow edge `ref → referent` is established so dropping the referent inside the callee flags E509 on subsequent ref reads. |

### MEDIUM — Conservative but lossy

| ID | Status | File:Line | Description |
|----|--------|-----------|-------------|
| **M1** | [x] 🟢 Source → ✅ Fixed | `StmtAnalysis.cpp:384-474` | **W510 only scans then_state keys.** If a variable exists only in the else branch and is Live there, no asymmetry warning is emitted. The variable appears Live after merge, potentially producing a confusing E501 later. Fixed with full lexical scope tracking: `analyzeScopedBlock`/`analyzeScopedStmt` save pre-block state and restore/remove variables declared inside blocks. W510 extended to warn when a variable is Live in one branch but absent from the other. |
| **M2** | [x] 🟢 Source → ✅ Fixed | `StmtAnalysis.cpp:490-530` | **Loop body analyzed once — conditional destruction now caught at if/else merge points.** When a variable Live before the loop is destroyed (dropped/moved/escaped) on one branch of an if/else inside the loop but not the other, the merged state previously masked it via `join(Dropped, Live) = Live`. Now the `loop_pre_live` set tracks which vars were Live pre-loop, and any branch asymmetry involving them triggers E506 directly at the merge point. Complements the existing unconditional E506 check. |
| **M3** | [x] 🟢 Source | `Chaperone.cpp:28-32` | **`join(Escaped, Moved) = Dropped`.** Both are "gone" states but for different reasons. The E503 message for the `Dropped` state was changed from "Double denaturation" to "Cannot drop" to avoid misleading on the Escaped path — the body already lists all three reasons (dropped/moved/escaped). Resolution: improved error message in `StmtAnalysis.cpp:348-352`. |
| **M4** | [x] 🟢 Source → ✅ Verified | test coverage | **No test for mutual recursion.** The fixed-point should handle A→B→A patterns but this is untested. → Added `negative/34_e503_mutual_recursion_cycle.an`: true A→B→A cycle where `a` calls `b` and `b` calls `a` back, then drops. Fixed-point converges and detects E503 in `main`. |
| **M5** | [x] 🟢 Source → ✅ Verified | test coverage | **No test for RangeExpr or InterpStringExpr in tracked contexts** (C1, C2). → Added positive tests 15 (RangeExpr) and 16 (InterpStringExpr) — clean compilation of tracked variables in range expressions and interpolated strings with no false positives. |

### LOW — Cosmetic / documentation

| ID | Status | File:Line | Description |
|----|--------|-----------|-------------|
| **LOW1** | [x] 🟢 Source → ✅ Fixed | `StmtAnalysis.cpp:492-573` | Loop condition re-analysis uses merged post-body state, not the exact state the next iteration would see. Fixed: `analyze_loop_body` returns exact `body_state`; condition/increment re-analysis uses it directly while post-loop state uses explicit `join_maps`. |
| **LOW2** | [x] 🟢 Source → ✅ Verified | test coverage | No variadic function interprocedural test. Added: positive 17 (variadic function borrows tracked param, caller drops) and negative 35 (variadic function consumes tracked param → E503 double-drop). |
| **LOW3** | [x] 🟢 Source → ✅ Verified | test coverage | No indirect/function-pointer call test. Added: positive 18 (indirect call through function pointer with borrowed arg — documents that Chaperone treats unknown callees as Borrowed). |

---

## Part 3: What Works (Keep / Build On)

These are sound, verified, and the foundation to extend:

- **Per-variable state machine** — `Uninit`/`Live`/`Dropped`/`Escaped`/`Moved` states with correct transitions for all tracked types.
- **E501 leak on return/throw/function-exit** — correctly excludes borrowed parameters.
- **E502 use-after-free** — thorough `analyzeExpr` walk covers all expression types except RangeExpr (C1) and InterpStringExpr (C2).
- **E503 double-drop** — correct for direct variable references.
- **S1 move-on-assign** — `unique_ptr` semantics prevent aliasing double-frees.
- **S6 field-ownership moves** — `this.f = tracked` transitions source to Moved.
- **S7 reassignment leak** — `Live x = new_value` reports E501.
- **Merge lattice** — correct for Live/Dropped/Escaped/Moved; W510 warns on asymmetry.
- **M1 lexical scope tracking** — variables declared inside blocks are removed from the state map when the block exits; shadowed variables restore their pre-block state.
- **E506 loop-body drop** — catches drop-without-reassign inside loops.
- **Drop cascades (for class/owned-data structs)** — walks field declarations in reverse order.
- **data copy-on-assign** — scoped to plain data only.
- **@unsafe → warn-don't-error** — state still flows through.
- **S8 finally protection** — `finally { drop x; }` discharges throw-path leak obligation.
- **S3 ref<T> borrow check** — E509 at ref-use time when referent is Dropped/Moved.
- **Interprocedural fixed-point** — forward references and recursion converge (modulo C3/H4).
- **29-test Chaperone suite** — comprehensive for the currently-tracked type set.

---

## Part 4: Remediation Plan

### Phase A — Fix the Leak: Built-in Type Finalization (L1–L15)

**Goal:** Every heap allocation has a path to `free`. The Chaperone can verify
it for tracked types; untracked types need runtime finalization.

**Approach:** Implement `__ang_gc_finalize` to recursively free interior
pointers of all built-in types. This is the same job the old GC's finalizer
did. The function reads the `ObjHeader.type` field and dispatches:

```
switch (header->type):
    OBJ_STRING:      free(chars_buffer)
    OBJ_LIST:        for each element: if TAG_OBJ, free(payload); free(element_buffer)
    OBJ_RECORD:      for each entry: free(key); if TAG_OBJ, free(value); free(entry_buffer)
    OBJ_EXCEPTION:   if message is TAG_OBJ, free(message)
    OBJ_CLOSURE:     for each captured var: if TAG_OBJ, free(var); free(env)
    OBJ_RAW_ARRAY:   free(element_buffer)
    OBJ_VECTOR:      (nothing — overallocated inline, freed with struct)
    OBJ_BOUND_METHOD, OBJ_TRAIT_OBJECT, OBJ_THREAD, OBJ_MUTEX, OBJ_NATIVE_INSTANCE:
                      (nothing interior to free)
```

Then in `cgDrop`, after the cascade loop, the final `__ang_gc_finalize` call
(which is already emitted) would actually do the work. Additionally, `drop`
should be extended to accept any type — the Chaperone would track built-in
types as `Live` if declared with `owned` syntax (Phase B).

**Files:** `angc/backend/llvm/rt/Memory.cpp` (implement `__ang_gc_finalize`),
`angc/backend/llvm/stmt/StmtCodegen.cpp` (cgDrop already emits the call).

**Status:** [x] Complete — `__ang_gc_finalize` dispatches on ObjHeader.type and frees
interior buffers (chars, elements, entries, env, buf, args, name, finalizer).
cgDrop cascade extended to all heap-allocated field types (L14, L15).
`drop` now allowed on any heap-allocated type (not just tracked types).

### Phase B — Track Built-in Types (optional, for verification)

**Goal:** The Chaperone can verify that built-in types are properly freed.
Currently it ignores them. With Phase A's finalizer in place, enabling
tracking would let the Chaperone detect leaks of strings, lists, etc.

**Current state (partial):** Phase A already provides the runtime cleanup
path (`__ang_gc_finalize` frees interior buffers). The DropStmt handler
already allows `drop` on any heap-allocated type (including local variables
after the fix in commit `7efe829`). Double-drop (E503) and use-after-free
(E502/E507) detection both work for built-in types — they transition through
Dropped/Moved/Escaped states. **The only gap is E501 leak detection:**
built-in types are `Uninit`, not `Live`, so the Chaperone never warns when
you forget to drop them.

**Approach:** Extend `collectTrackedTypes` / `isTrackedTypeObj` to optionally
include built-in types so they enter the `Live` state. This could be gated
on a new `owned string` / `owned list` syntax as described in `CHAPERONE.md`
decision #11, or applied universally in a new "strict" mode.

**Files:** `Chaperone.cpp` (collectTrackedTypes, isTrackedTypeObj),
`StmtAnalysis.cpp` (DropStmt handler, VarDeclStmt handler),
`StmtCodegen.cpp` (cgDrop already handles non-class structs).

**Status:** [x] Complete — `isTrackedTypeObj` returns true for all `isHeapAllocatedType` types (string, list, record, vector, raw_array, closures, etc.). Built-in types enter `Live` state. Leak diagnostics for built-in types use W521 (warning, does not halt compilation); class/owned-data leaks remain E501 (error). E505 now only fires for truly untracked containers (tuples). 52/52 Chaperone tests pass.

### Phase C — Fix Control-Flow Gaps

**C1 (RangeExpr):** [x] Done — `case RangeExpr` added to `analyzeExpr` in `ExprAnalysis.cpp`.

**C2 (InterpStringExpr):** [x] Done — `case InterpStringExpr` added to `analyzeExpr`.

**C3 (Method summary name collision):** [x] Done — summaries keyed by qualified name
`"ClassName.methodName"`.

**C4 (Closure body analysis):** [x] Done — LambdaExpr handler walks closure body.

**H1 (TryStmt in collectVarRefs):** [x] Done — `case TryStmt` added to `collectVarRefs`.

**H2 (Closure call summaries):** [x] Done — LambdaExpr handler builds a FunctionSummary
from the body analysis (keyed by the LambdaExpr's unique FunctionType pointer).
CallExpr handler looks up closure summaries via the callee VarExpr's type when
no named-function summary is found. Lambda params are seeded into the body
analysis state and excluded from E501 leak checks (save/restore ctx.current_params).
Tests: 29 (E502 closure-drops-arg), 30 (E503 closure-drops-then-drop),
09 + 10 (positive: borrow and drop-via-closure).

**H3 (@consumes/@escape):** [x] Done — Parser recognizes `@consumes(i, j)` and
`@escape(i, j)` annotations on function declarations. Summary building uses
annotations (takes precedence over inference). Annotated foreign functions
have their summaries pre-built before the fixed-point loop.

**H4 (Oscillation detection):** [x] Done — W521 warning when summary hashes repeat.

**H5 (Interprocedural borrows):** [x] Done — `analyzeFunction` seeds `ctx.borrows` for `ref<T>` params
when the function also receives a tracked param of matching type T. Dropping the tracked param
inside the callee now correctly flags E509 on subsequent ref reads. Tests: 31 (negative: drop referent
then read ref), 11 (positive: read ref then drop referent).

### Phase D — Test Coverage

| Test | Covers |
|------|--------|
| RangeExpr with tracked var | C1 | 15 (positive), 28 (negative) |
| InterpStringExpr with tracked var | C2 | 16 (positive), 23 (negative) |
| Two classes with same-named method, one drops one borrows | C3 |
| Closure body containing `drop` then `use` | C4 |
| Closure inside try/catch capturing tracked var | H1 |
| Mutual recursion (A→B→A cycle) | M4 | 34 (negative) |
| `ref<T>` passed to another function that drops referent | H5 | 31 (negative), 11 (positive) |
| Loop-body conditional drop (if/else, one branch drops) | M2 | 32 (negative) |
| Loop-body conditional move (if/else, one branch moves) | M2 | 33 (negative) |
| Loop-body conditional drop + reassign (no false positive) | M2 | 14 (positive) |
| Scoped block cleanup (variable declared in one branch, dropped) | M1 | 12 (positive) |
| Shadow restore (inner block shadows outer variable) | M1 | 13 (positive) |
| Variadic function interprocedural (borrow + consume) | LOW2 | 17 (positive), 35 (negative) |
| Indirect/function-pointer call (treated as Borrowed) | LOW3 | 18 (positive) |

---

## Part 5: Progress Tracking

| ID | Description | Status |
|----|-------------|--------|
| **L1** | string allocations leak | [x] |
| **L2** | string concat (copy path) leaks | [x] |
| **L3** | list allocations leak | [x] |
| **L4** | record allocations leak | [x] |
| **L5** | exception objects leak | [x] |
| **L6** | closure objects leak | [x] |
| **L7** | bound_method objects leak | [x] |
| **L8** | trait_object objects leak | [x] |
| **L9** | raw_array objects leak | [x] |
| **L10** | vector objects leak | [x] |
| **L11** | thread struct leak | [x] |
| **L12** | mutex struct leak | [x] |
| **L13** | native_instance leak | [x] |
| **L14** | class field sub-buffers leak | [x] |
| **L15** | nested owned field sub-buffers leak | [x] |
| **C1** | RangeExpr not analyzed | [x] |
| **C2** | InterpStringExpr not analyzed | [x] |
| **C3** | Method summary name collision | [x] |
| **C4** | Closure bodies never analyzed | [x] |
| **H1** | TryStmt missing from collectVarRefs | [x] |
| **H2** | No summary for closure calls | [x] |
| **H3** | @consumes/@escape not implemented | [x] |
| **H4** | No oscillation detection in fixed-point | [x] |
| **H5** | Borrow tracking intraprocedural only | [x] |
| **M1** | W510 only scans then_state keys | [x] |
| **M2** | Loop body analyzed once (no fixed-point) | [x] |
| **M3** | join(Escaped, Moved) = Dropped misleading | [x] |
| **M4** | No test for mutual recursion | [x] ✅ |
| **M5** | No test for RangeExpr/InterpStringExpr | [x] |
| **LOW1** | Loop condition re-analysis state mismatch | [x] |
| **LOW2** | No variadic function interprocedural test | [x] |
| **LOW3** | No indirect call test | [x] |

---

## Philosophy Guardrail (unchanged)

> The Chaperone is a **verifier that reports where it cannot prove safety**,
> not a borrow checker that rejects patterns. There are **no lifetimes in the
> type system**. When the Chaperone can't prove safety (opaque FFI, unconverged
> recursion), it reports and the programmer opts out via `@unsafe`/`@manual`.
> **Stay a verifier. Do not clone Rust.**

This audit began with two goals: (1) fix the runtime so every allocation has
a path to `free` (Phase A), and (2) extend the Chaperone's field of view so
it can verify cleanup of all heap-allocated types, not just class/owned-data
(Phase B).  Both are now complete.

---
## Closing Summary (2026-07-05)

All 30 items across seven categories are resolved:

| Category | Count | Result |
|----------|-------|--------|
| L1–L15 | 15 | `__ang_gc_finalize` dispatches on `ObjHeader.type`, frees all interior buffers; cgDrop cascade extended to all heap-allocated field types |
| C1–C4 | 4 | Critical analysis gaps fixed (RangeExpr, InterpStringExpr, method name collision, closure body analysis) |
| H1–H5 | 5 | High-priority gaps fixed (TryStmt capture, closure summaries, @consumes/@escape, oscillation detection, interprocedural borrows) |
| M1–M5 | 5 | Medium items resolved (scope tracking, loop conditional destruction, error messages, mutual recursion + expression tests) |
| LOW1–LOW3 | 3 | Test coverage filled (loop condition precision, variadic interprocedural, indirect calls) |
| Phase A | — | Runtime finalization for all 13 built-in types |
| Phase B | — | Built-in types tracked as `Live`; W521 warnings for leaks; E501 errors for class/owned-data |
| Phase C | — | All control-flow gaps fixed |
| Phase D | — | 52 Chaperone tests + 2 LSP tests — all passing |

**The Chaperone now sees every heap allocation.** Class, owned data, string,
list, record, vector, raw array, closure, bound method, trait object,
exception, thread, mutex, and native instance — all enter the `Live` state
and are subject to drop tracking, use-after-free detection, and leak
diagnostics. Built-in type leaks emit W521 warnings (compilation succeeds);
class and owned-data leaks remain E501 errors (compilation halts).
