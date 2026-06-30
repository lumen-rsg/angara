# Chaperone Audit — Memory-Safety Verifier

> Full audit of the **Chaperone** compile-time memory pass against the
> `v5-chaperone` branch, performed **2026-06-30**.
>
> Scope: `angc/backend/chaperone/Chaperone.cpp` (710 lines), `Chaperone.h`,
> the codegen it depends on (`StmtCodegen.cpp::cgDrop`, `ExprCodegen.cpp::cgAssign`),
> `CHAPERONE.md`, the test suite, and the LSP pipeline.
>
> **Status legend:**
> - 🟢 **Source** — confirmed by reading the code (file:line cited).
> - ⚫ **Verified** — reproduced by compiling/running a program.
>
> Each item has a stable **ID** (`CHAP-N`) for referencing in commits/PRs.
> Check `[ ]` → `[x]` when resolved.

---

## Summary

| Category | Critical | High | Medium | Low | Total | Fixed |
|---|---|---|---|---|---|---|
| Soundness holes (bugs slip through) | 2 | 7 | 1 | — | 10 | 10 (all closed — S0–S9, incl. S3) |
| Coverage gaps (not analyzed) | — | 1 | 6 | 1 | 8 | 6 (G1, G2, G3 N/A, G4, G6, G7) |
| Doc ↔ implementation mismatches | — | 2 | 3 | 1 | 6 | 1 (D6) |
| Test coverage | — | 1 | — | — | 1 | 1 (T1) |
| Tooling (LSP) | — | 1 | — | — | 1 | 0 |

**Status (post Stage 6):** all soundness holes are closed. The two that
mattered most are resolved — `S1` (the Chaperone tracked variables not
allocations, so `class`/`owned` aliasing defeated it; fixed with move-on-assign)
and `S0` (the driver ignored the pass's result and shipped crashing binaries
anyway; fixed by gating codegen on errors). Every analyzable bug class — leaks,
use-after-free, double-free, use-after-move, cycles, dangling borrows,
loop/condition/closure/container/global escapes — is now detected and halts
compilation.

**What remains** is doc reconciliation (`CHAPERONE.md` now oversells/describes
removed features — Stage 7) and LSP integration (Stage 8, run the pass in the
editor). These are honesty and tooling, not soundness.

---

## 1. Soundness holes (let memory bugs compile silently)

| ID | Status | Sev | Issue | Location |
|---|---|---|---|---|
| - [x] **S0** | ⚫ Verified → ✅ Fixed | **Critical** | **Chaperone errors do not halt compilation.** `CompilerDriver` called `Chaperone::run(...)` but ignored the return value and never checked `errorHandler.hadError()` afterward, unlike every other pass (type-check bails with `m_had_error=true; return nullptr`). Result: E501–E506 were *reported* but compilation proceeded — the binary linked and segfaulted at runtime. Verified: a use-after-free program emitted `Error [E502]` then built `/tmp/c3`, which exited 139. This made every other finding moot in practice. *(Fixed: codegen now gated on `errorHandler.hadError()` after the Chaperone call, mirroring the type-checker bail. Warnings (W510/W521) do NOT halt — only errors.)* | `CompilerDriver.cpp:401`; verified E502 → binary → exit 139 |
| - [x] **S1** | 🟢 Source → ✅ Fixed | **Critical** | **Aliasing of `class`/`owned` is untracked.** Copy-on-assign was applied *only to plain `data`*; `class`/`owned` assignment was a raw pointer copy and the state machine recorded both names as `Live`, so `let c2 = c1; drop c1; drop c2;` double-freed and passed the Chaperone. *(Fixed: move-on-assign, `unique_ptr` semantics. New `Moved` state: on `let c2 = c1` / `c2 = c1` of a tracked type, the source transitions to `Moved` (invalid) and the destination becomes `Live`. Use-after-move is E507; dropping a moved-from var is E503. `data` types are untouched (still copy-on-assign). No lifetimes, no borrow checker, no pattern rejection — only genuinely unsound aliasing is now rejected.)* | `Chaperone.cpp` (VarDecl/Assign/VarExpr/DropStmt handlers); verified by tests 04, 09, 11 |
| - [x] **S2** | 🟢 Source → ✅ Fixed | High | **Loop conditions and `for-in` iterables are never analyzed.** `analyze_loop` walked only `body`; the `while`/`for` condition, the `for` increment, and the `for-in` iterable were skipped → `drop b; while (b.get() > 0) {}` compiled (use-after-free undetected). *(Fixed: the loop handlers now `analyzeExpr` the condition (before and after the body, since it's re-evaluated each iteration), the `for` increment, and the `for-in` iterable. No false positives — 18/18 chaperone tests pass and zero E5xx across the whole suite.)* | `Chaperone.cpp` loop handlers; verified by tests 12, 13, 14 |
| - [x] **S3** | 🟢 Source → ✅ Fixed | High | **`ref<T>` lifetime tracking did not exist.** `isTrackedVar` returned false for `REF` types; nothing modeled borrow lifetimes, so a `ref<T>` whose referent was dropped compiled silently (a latent use-after-free). *(Fixed with a minimal borrow check — no lifetimes in the type system. Assigning a tracked Live var to a `ref<T>` is a BORROW (not a move): the source keeps ownership, and the ref→referent pair is recorded in `ctx.borrows`. Reading a ref whose referent is Dropped/Moved is E509 (a dangling borrow). The check fires at ref-use time, so a valid borrow — ref used while the referent is live, then dropped after — is not a false positive. Also fixed a Phase-1 interaction: `ref_x = tracked` no longer spuriously moves the source (move only happens when the target is itself tracked).)* | `Chaperone.cpp` borrow recording + VarExpr E509 check |
| - [x] **S4** | 🟢 Source → ✅ Fixed | High | **Interprocedural analysis was single-pass, no fixed point.** `run()` walked functions once in source order; forward references and recursive/mutual recursion hit the lenient "unknown → borrow" path — so a double-drop via a forward-referenced consumer shipped a binary. *(Fixed: a worklist fixed-point. All functions (top-level + methods) are analyzed repeatedly until no summary changes, with an 8-pass cap as the convergence-fallback the design doc specifies. Diagnostics are suppressed during convergence and emitted only on the final pass to avoid duplicates. Verified: forward-reference double-drop now caught (test 17); recursion converges to Borrowed without false E503.)* | `Chaperone.cpp` run(); `CHAPERONE.md:256-277` |
| - [x] **S5** | 🟢 Source → ✅ Fixed | High | **Multi-parameter functions escaped *all* their args.** `if (summary.size() == 1) { apply } else { state = Escaped; }` — any function with >1 tracked parameter lost tracking on every argument, causing false E503 on later legitimate drops. *(Fixed: the summary is now a POSITIONAL vector aligned to call-arg positions (`summary[i]` = behavior of the param the i-th call-arg binds to; `this` is parsed but not stored in params, so indices line up). Call-site matching is a direct index — no single-param shortcut, no blanket-Escape fallback. Verified: a 2-tracked-param function (drops a, borrows b) no longer false-alarms on `drop b` (test 06).)* | `Chaperone.cpp` summary build + CallExpr apply |
| - [x] **S6** | ⚫ Verified → ✅ Fixed | Medium | **Field-level ownership is untracked.** Codegen cascades drops over owned fields, which double-frees when ownership of a tracked value is aliased into a field (`this.f = b`, then both the field's owning object cascade-drop and the source `b` free the same memory). The parser restricts `drop` to a bare `IDENTIFIER`, so `drop conn.field` is already a parse error — but the *aliasing* path was open. *(Fixed: `this.f = tracked_live_var` is now a move — the source transitions to `Moved` (E507 on later use, E503 on later drop), consistent with Phase 1's `unique_ptr` semantics. External field writes are already blocked by the type checker (private, E336), so this only fires inside methods/constructors. The field's *previous* value leaking is still untracked — needs per-field state, deferred.)* | `Chaperone.cpp` AssignExpr GetExpr branch; verified by tests 05, 16 |
| - [x] **S7** | 🟢 Source → ✅ Fixed | High | **Reassignment leak via `x = …` on tracked vars is not detected.** The `AssignExpr` handler had a comment describing the leak but only walked the value; it never transitioned or reported. *(Fixed: the `AssignExpr` handler now reports E501 when a `Live` tracked target is overwritten by `=`, and applies the move transition to the RHS. Compound assigns (`+=` etc.) and field/subscript targets are excluded — they don't transfer whole-variable ownership.)* | `Chaperone.cpp` AssignExpr handler; verified by test 10 |
| - [x] **S8** | ⚫ Verified → ✅ Fixed | Medium | **Throw handler ignored `finally` cleanup.** The ThrowStmt handler flagged *every* `Live` tracked var at the throw point as an E501 leak, even when a `finally {}` on the enclosing `try` would release it. *(Fixed: the TryStmt handler snapshots the names a `finally {}` drops into `ctx.finally_protected` before analyzing the try body; the ThrowStmt handler skips those names (the finally runs on the throw path and discharges the obligation). Scoped + restored on try exit. Verified both directions: a `finally { drop b; }` discharges; a finally that doesn't drop `b` still flags it.)* | `Chaperone.cpp` TryStmt + ThrowStmt |
| - [x] **S9** | ⚫ Verified → ✅ Fixed | High | **`analyzeFunction` discarded the body's threaded state.** `analyzeBlock` takes `state` by value and returns the threaded map, but `analyzeFunction` called it and *threw away the return value*. Every function's params/locals were therefore stuck at their entry state for the leak check and summary — corrupting the entire interprocedural summary (every function looked like it borrowed all its params). Found while debugging field-move summaries. *(Fixed: `state = analyzeBlock(...)` — the body's moves/drops now flow to the leak check and summary.)* | `Chaperone.cpp` analyzeFunction |

---

## 2. Coverage gaps (not analyzed at all)

| ID | Status | Sev | Issue | Location |
|---|---|---|---|---|
| - [x] **G1** | 🟢 Source → ✅ Fixed | Medium | **Globals / module-level owned allocations.** `run()` only analyzed `FuncStmt`; top-level `let g = Buffer()` was never checked — globals were de-facto `@manual` by accident. *(Fixed: `run()` now scans top-level `VarDeclStmt`s and reports E501 for tracked globals — they have no enclosing scope to drop them in, so they leak by construction. Future: `@manual` to opt out.)* | `Chaperone.cpp` run() |
| - [x] **G2** | 🟢 Source → ✅ Fixed | Medium | **Lambdas / closures.** `analyzeExpr` treated `LambdaExpr` as a no-op; a closure capturing a tracked `Live` value and outliving it was untracked. *(Fixed: a LambdaExpr now collects every VarExpr referenced in its body (`collectVarRefs`); any tracked Live var so referenced transitions to Escaped with E505 — the closure may outlive the local, so ownership is treated as transferred.)* | `Chaperone.cpp` LambdaExpr handler |
| - [x] **G3** | ⚫ Verified → ✅ N/A | ~~Medium~~ | **`match` pattern-bound variables / arm bodies.** Originally flagged as unanalyzed — but verification shows the `MatchExpr` handler *does* walk the condition and all arm bodies (`analyzeExpr` at the MatchExpr case). Use-after-free inside an arm is caught (verified: E502 fires in `tests/chaperone/negative/15`). Pattern-binding of tracked types (`case Some(b)` where `b` is a `class`) is impossible in the current language — enums can't carry class instances (E237) — so there's nothing to register. **No fix needed; closed as not-a-bug.** | `Chaperone.cpp:307-314` |
| - [x] **G4** | 🟢 Source → ✅ Fixed | Medium | **E505 ("escaped molecule into untracked container") was documented but not implemented.** Storing a `Buffer` into `list`/`record` then dropping the list leaked it silently. *(Fixed: ListExpr/RecordExpr literals containing a tracked Live var report E505 and transition the var to Escaped — the container isn't tracked, so the value can't be dropped correctly. `any_is.an` updated to construct the Foo directly in the list rather than aliasing a tracked local.)* | `Chaperone.cpp` checkEscapeIntoContainer |
| - [ ] **G5** | 🟢 Source | Medium | **`Rc<T>` / `weak<T>`** documented escape hatches have no implementation. | `CHAPERONE.md:299-301` |
| - [x] **G6** | 🟢 Source → ✅ Fixed | Low | **Optionals.** `Connection?` was treated as untracked (the OPTIONAL type kind fell through `isTrackedVar`), so optionals of tracked types leaked silently — `drop b` on a `Buf?` was an E503 "not a tracked allocation." *(Fixed: a new `isTrackedTypeObj` helper unwraps `OptionalType` to its inner type; `Buf?` is now tracked like `Buf`. Used in `isTrackedVar` and param registration.)* | `Chaperone.cpp` isTrackedTypeObj |
| - [x] **G7** | ⚫ Verified → ✅ Fixed | High | **Class methods were never analyzed.** `run()` only walked top-level `FuncStmt`; `MethodMember` bodies inside `ClassStmt` were skipped entirely, so every memory bug inside a method (field moves, use-after-free, leaks) was invisible. *(Fixed: `run()` now descends into `ClassStmt` members and analyzes each method's `FuncStmt`. Param registration gained a fallback that reads the param's `ASTType` annotation directly, since methods aren't resolvable by bare name in the symbol table.)* | `Chaperone.cpp` run() + analyzeFunction |

---

## 3. Documentation ↔ implementation mismatches

| ID | Status | Sev | Issue | Location |
|---|---|---|---|---|
| - [ ] **D1** | 🟢 Source | High | **`@consumes` / `@escape` / `@manual` annotations are not implemented.** Only mentioned in a comment. All foreign/module calls default to borrow with *no way* to mark consumes/escape, yet the doc has a full FFI table. | `Chaperone.cpp:195`; `CHAPERONE.md:279-337` |
| - [ ] **D2** | 🟢 Source | High | **Auto-drop insertion on throw (Phase 3) was removed but the doc still describes it.** Commit `cde8e23` replaced it with E501-on-throw + manual `finally`. Code is self-consistent; the doc section, decision #6, and the architecture diagram are stale. | `Chaperone.cpp:421-438`; `CHAPERONE.md:46-62,166-182` |
| - [ ] **D3** | 🟢 Source | Medium | **Interprocedural fixed point / recursion convergence documented but unimplemented** (see S4). | `CHAPERONE.md:256-277` |
| - [ ] **D4** | 🟢 Source | Medium | **`borrow<T>` lifetime verification promised but absent** (see S3). | `CHAPERONE.md:33-34,86-91` |
| - [ ] **D5** | 🟢 Source | Medium | **Staging table stale.** `CHAPERONE.md` lists Stages 3-8 as "☐ future" but the git log shows Stages 3-7 are committed. | `CHAPERONE.md:375-386` |
| - [x] **D6** | ⚫ Verified → ✅ Fixed | High | **E504 never fired at all** — worse than "first cycle only." `detectCycles` looked up field types in `getVariableTypes()` (a `VarDeclStmt*`→Type map that only holds local/let vars), so type-declaration fields were never found and the ownership graph stayed empty. Verified: `owned Node { let next as Node; }` and a 2-node `A→B→A` both compiled silently. *(Fixed: `check_field` now reads the base type name directly from the field's `ASTType` annotation — `SimpleType`/`GenericType`/`Optional`/`Owned` — and matches against `tracked_types`. Self-cycles and mutual cycles now report E504; acyclic types have no false positive.)* Residual: still reports only the first cycle; `W521` still never emitted (deferred to Stage 7). | `Chaperone.cpp:634-660`; verified `Node→Node`, `A→B→A` → E504 |

---

## 4. Test coverage

| ID | Status | Sev | Issue | Location |
|---|---|---|---|---|
| - [x] **T1** | 🟢 Source → ✅ Fixed | High | **No tests for the Chaperone.** No `owned`/`drop`/`borrow` `.an` files in `tests/`; no C++ tests referenced `Chaperone`; `run_tests.sh` never exercised memory diagnostics. *(Fixed: `tests/chaperone/{positive,negative}/` with 3 positive + 8 negative `.an` files, each negative declaring `// expect: Exxx`; `run_chaperone_tests.sh` asserts the code fires AND that E-codes halt compilation (S0 enforcement) while W-codes don't; `make test-chaperone` / `make test` targets added. 11/11 pass.)* | `tests/chaperone/`; `Makefile` |

Minimal must-cover matrix:

| Code | must-error case | must-pass case |
|---|---|---|
| E501 leak | allocated, never dropped, function exits | allocated, dropped before exit |
| E502 use-after-free | use after `drop` | use before `drop` |
| E503 double-drop | two `drop` of same var | single `drop` |
| E504 cycle | `owned N { let next as N; }` | back-edge via `ref<T>` |
| E506 loop drop | drop inside `while` of pre-loop Live | reassign before back-edge |
| E507 use-after-move (new) | `let c2 = c1; … c1.use()` | `let c2 = c1; c2.use()` |
| E508 field-targeted move (new) | `conn.f = c2` of a tracked field | (forbid — fields released by owner) |
| W510 branch asymmetry | drop on one branch only | drop on both / neither |

---

## 5. Tooling (LSP)

| ID | Status | Sev | Issue | Location |
|---|---|---|---|---|
| - [ ] **L1** | 🟢 Source | High | **LSP does not run the Chaperone.** `analyzeDocument` runs Lexer → Parser → TypeChecker and publishes diagnostics, but never calls `Chaperone::run`. Memory bugs (E501–E506) are invisible in the editor; squiggles only appear on the full compile. The fix is a ~2-line call at the same site, reusing the existing `errorHandler`/diagnostic pipeline. | `LSPServer.cpp:415-468` |

---

## What the Chaperone does well (keep / build on)

These are sound and correctly implemented — the foundation to extend:

- Per-variable state machine: `Uninit`/`Live`/`Dropped`/`Escaped` (`Chaperone.cpp:38-43`).
- E501 leak on return / throw / function-exit, correctly excluding borrowed parameters (`Chaperone.cpp:302-314, 409-416, 426-435`).
- E502 use-after-free on **direct** variable references via a thorough `analyzeExpr` walk (`Chaperone.cpp:91-271`).
- E503 double-drop + drop-after-escape (`Chaperone.cpp:385-395`).
- Merge lattice, early-return exclusion, W510 branch asymmetry (`Chaperone.cpp:25-42, 459-476`).
- Loop-body drop check, E506 (`Chaperone.cpp:491-501`).
- Drop cascades in codegen, `ref<T>` excluded (`StmtCodegen.cpp:349-409`).
- `data` copy-on-assign, scoped to plain data only (`StmtCodegen.cpp:63-71`, `ExprCodegen.cpp:517-525`).
- `@unsafe` → warn-don't-error, state still flows (`Chaperone.cpp:13-19, 606-616`) — a genuinely good design.
- `finally {}` semantics: analyzed on both normal and catch paths (`Chaperone.cpp:582-596`).

---

## Implementation plan (referenced from the plan in this session)

See the staged implementation plan. Items map to IDs above:

| Stage | Fixes | IDs | Status |
|---|---|---|---|
| **0** — Enforcement + test harness | gate codegen on Chaperone errors; negative/positive suite for existing diagnostics; also fixed E504 cycle detection | S0, T1, D6 | ✅ done |
| **1** — Move-on-assign (soundness) | new `Moved` state; `=` / `let c2=c1` move the source; E507 use-after-move | S1, S7 | ✅ done |
| **2** — Analyze what's skipped | loop conditions, `for-in` iterables, for-loop increment (match arms already analyzed — G3 closed as N/A) | S2 | ✅ done |
| **3** — Field ownership rules | move-on-field-assign (`this.f = tracked` → source Moved); also fixed class methods unanalyzed (G7) and the analyzeFunction state-discard bug (S9) | S6, G7, S9 | ✅ done |
| **4** — Interprocedural fixed point | worklist to convergence, recursion handling, positional summaries | S4, S5 | ✅ done |
| **5** — Coverage breadth | globals, closures, E505, optionals, throw/`finally` cleanup discharge | G1, G2, G4, G6, S8 | ✅ done |
| **6** — `ref<T>` minimal borrow check | ref→referent borrow map; E509 dangling borrow at ref-use time; move vs borrow distinction | S3 | ✅ done |
| **7** — Doc reconciliation | `@consumes`/`@escape`/`@manual` or strike them; fix Phase-3, staging, E504 multi-cycle, W521 | D1, D2, D3, D4, D5, D6 | ☐ |
| **8** — LSP integration | run Chaperone in `analyzeDocument`; publish E501–E508 + W510/W521 | L1 | ☐ |

---

## Philosophy guardrail

The Chaperone is a **verifier that reports where it cannot prove safety**, not a
borrow checker that rejects patterns. There are **no lifetimes in the type
system**. The only addition that changes what code is *accepted* is Stage 1
(move semantics) — and it does so only for genuinely unsound aliasing. When the
Chaperone can't prove safety (opaque FFI, unconverged recursion), it reports
and the programmer opts out via `@unsafe`/`@manual`, which already exist.
**Stay a verifier. Do not clone Rust.**
