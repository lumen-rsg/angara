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

| Category | Critical | High | Medium | Low | Total |
|---|---|---|---|---|---|
| Soundness holes (bugs slip through) | 1 | 6 | — | — | 7 |
| Coverage gaps (not analyzed) | — | — | 6 | — | 6 |
| Doc ↔ implementation mismatches | — | 2 | 3 | 1 | 6 |
| Test coverage | — | 1 | — | — | 1 |
| Tooling (LSP) | — | 1 | — | — | 1 |

**The single most important issue:** the Chaperone tracks *variables*, not
*allocations*. `class`/`owned` assignment aliases two names to one heap object
(`S1`). Until that is fixed, the core promise — "catches use-after-free and
double-free" — does not hold whenever ownership is shared by more than one name.

---

## 1. Soundness holes (let memory bugs compile silently)

| ID | Status | Sev | Issue | Location |
|---|---|---|---|---|
| - [ ] **S1** | 🟢 Source | **Critical** | **Aliasing of `class`/`owned` is untracked.** Copy-on-assign is applied *only to plain `data`*; `class`/`owned` assignment is a raw pointer copy and the state machine records both names as `Live`. After `let c2 = c1; drop c1; drop c2;` both the source and the duplicate "own" the freed object → **double-free passes the Chaperone**. The design tracks variables; allocations must have exactly one owner. Fix = move-on-assign (`unique_ptr` semantics): on `let c2 = c1` / `c2 = c1` of a tracked type, the source transitions to a new `Moved` state; use-after-move is flagged. No lifetimes, no borrow checker, no pattern rejection. | `ExprCodegen.cpp:517-525`; `StmtCodegen.cpp:63-71`; `Chaperone.cpp:38-43` |
| - [ ] **S2** | 🟢 Source | High | **Loop conditions and `for-in` iterables are never analyzed.** `analyze_loop` walks only `body`; the `while`/`for` condition and `for-in` iterable are skipped → `drop b; while (b.size() > 0) {}` compiles (use-after-free undetected). | `Chaperone.cpp:505-517` |
| - [ ] **S3** | 🟢 Source | High | **`borrow<T>` / `ref<T>` lifetime tracking does not exist.** `isTrackedVar` explicitly returns false for `REF` types; nothing models borrow lifetimes. A `ref<T>` that outlives its referent compiles silently — directly contradicting decision #7 in `CHAPERONE.md`. | `Chaperone.cpp:77`; `CHAPERONE.md:33-34,86-91` |
| - [ ] **S4** | 🟢 Source | High | **Interprocedural analysis is single-pass, no fixed point.** `run()` walks functions once in source order; the documented whole-program fixed point, recursion handling, and convergence-fallback are unimplemented. Calls to later-defined functions and recursive/mutual recursion hit the lenient "unknown → borrow" path. | `Chaperone.cpp:700-706`; `CHAPERONE.md:256-277` |
| - [ ] **S5** | 🟢 Source | High | **Multi-parameter functions escape *all* their args.** `if (summary.size() == 1) { apply } else { state = Escaped; }` — any function with >1 tracked parameter loses tracking on every argument, even when the summary says "borrowed." Causes false E503 on later drops and suppresses leak detection. Positional matching against the signature is needed. | `Chaperone.cpp:177-187` |
| - [ ] **S6** | ⚫ Verified | Medium | **Field-level ownership is untracked (currently masked).** The Chaperone never models `obj.field`. Codegen cascades drops over owned fields, which *would* double-free `buf` if `drop conn.buf` were allowed — but the parser restricts `drop` to a bare `IDENTIFIER` (`dropStatement.cpp:4`), so `drop conn.field` is already a **parse error** (verified, E502). Residual risk: once field moves via `conn.f = c2` (Stage 1) or droppable fields are introduced, the absence of field-state tracking becomes live. Add an explicit Chaperone rule rejecting field-targeted ownership transfers so the mask becomes intentional, not accidental. | `StmtCodegen.cpp:382-396`; `dropStatement.cpp:4` |
| - [ ] **S7** | 🟢 Source | High | **Reassignment leak via `x = …` on tracked vars is not detected.** The `AssignExpr` handler has a comment describing the leak but only walks the value; it never transitions or reports. Only `let` re-declaration catches overwrites. | `Chaperone.cpp:128-134` |

---

## 2. Coverage gaps (not analyzed at all)

| ID | Status | Sev | Issue | Location |
|---|---|---|---|---|
| - [ ] **G1** | 🟢 Source | Medium | **Globals / module-level owned allocations.** `run()` only analyzes `FuncStmt`; top-level `let g = Buffer()` is never checked. Globals are de-facto `@manual` by accident. | `Chaperone.cpp:700-705` |
| - [ ] **G2** | 🟢 Source | Medium | **Lambdas / closures.** `analyzeExpr` treats `LambdaExpr` as a no-op. A closure capturing a `Buffer` and outliving it is untracked. | `Chaperone.cpp:269` |
| - [ ] **G3** | 🟢 Source | Medium | **`match` pattern-bound variables** aren't registered as tracked bindings; block-bodied match arms aren't analyzed as scopes. | `Chaperone.cpp:261-267` |
| - [ ] **G4** | 🟢 Source | Medium | **E505 ("escaped molecule into untracked container") is documented but not implemented.** Storing a `Buffer` into `list`/`record` then dropping the list leaks it silently. | diagnostics table in `CHAPERONE.md`; no `E505` anywhere in `angc/` |
| - [ ] **G5** | 🟢 Source | Medium | **`Rc<T>` / `weak<T>`** documented escape hatches have no implementation. | `CHAPERONE.md:299-301` |
| - [ ] **G6** | 🟢 Source | Low | **Optionals.** `Connection?` is treated as Live regardless of nil-ness; dropping a possibly-nil optional is imprecise (codegen happens to be safe via nil-store). | `Chaperone.cpp:71-85` |

---

## 3. Documentation ↔ implementation mismatches

| ID | Status | Sev | Issue | Location |
|---|---|---|---|---|
| - [ ] **D1** | 🟢 Source | High | **`@consumes` / `@escape` / `@manual` annotations are not implemented.** Only mentioned in a comment. All foreign/module calls default to borrow with *no way* to mark consumes/escape, yet the doc has a full FFI table. | `Chaperone.cpp:195`; `CHAPERONE.md:279-337` |
| - [ ] **D2** | 🟢 Source | High | **Auto-drop insertion on throw (Phase 3) was removed but the doc still describes it.** Commit `cde8e23` replaced it with E501-on-throw + manual `finally`. Code is self-consistent; the doc section, decision #6, and the architecture diagram are stale. | `Chaperone.cpp:421-438`; `CHAPERONE.md:46-62,166-182` |
| - [ ] **D3** | 🟢 Source | Medium | **Interprocedural fixed point / recursion convergence documented but unimplemented** (see S4). | `CHAPERONE.md:256-277` |
| - [ ] **D4** | 🟢 Source | Medium | **`borrow<T>` lifetime verification promised but absent** (see S3). | `CHAPERONE.md:33-34,86-91` |
| - [ ] **D5** | 🟢 Source | Medium | **Staging table stale.** `CHAPERONE.md` lists Stages 3-8 as "☐ future" but the git log shows Stages 3-7 are committed. | `CHAPERONE.md:375-386` |
| - [ ] **D6** | 🟢 Source | Low | **E504 reports only the first cycle** and `ref<T>`/`borrow<T>`/container back-edges aren't recognized as the doc-prescribed cycle breaker (keys on `toString()`). `W521` is never emitted. | `Chaperone.cpp:685, 634-642, 667` |

---

## 4. Test coverage

| ID | Status | Sev | Issue | Location |
|---|---|---|---|---|
| - [ ] **T1** | 🟢 Source | High | **No tests for the Chaperone.** No `owned`/`drop`/`borrow` `.an` files in `tests/`; no C++ tests reference `Chaperone`; `run_tests.sh` never exercises memory diagnostics. Every code below would have caught its bug. A negative test suite (one must-fail + one must-pass per diagnostic code) is the highest-leverage process fix. | `tests/` |

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

| Stage | Fixes | IDs |
|---|---|---|
| **0** — Test harness + baseline | negative/positive suite for existing diagnostics | T1 |
| **1** — Move-on-assign (soundness) | new `Moved` state; `=` / `let c2=c1` move the source; E507 use-after-move | S1, S7 |
| **2** — Analyze what's skipped | loop conditions, `for-in` iterables, `match` arms/bindings, assignment leak | S2, G3, S7 |
| **3** — Field ownership rules | explicit Chaperone rule: forbid field-targeted ownership transfer; E508 | S6 |
| **4** — Interprocedural fixed point | worklist to convergence, recursion handling, positional summaries | S4, S5 |
| **5** — Coverage breadth | globals, closures, E505, optionals | G1, G2, G4, G6 |
| **6** — `ref<T>` minimal borrow check | scope-bound liveness of the referent at borrow scope-exit | S3 |
| **7** — Doc reconciliation | `@consumes`/`@escape`/`@manual` or strike them; fix Phase-3, staging, E504 | D1, D2, D3, D4, D5, D6 |
| **8** — LSP integration | run Chaperone in `analyzeDocument`; publish E501–E508 + W510/W521 | L1 |

---

## Philosophy guardrail

The Chaperone is a **verifier that reports where it cannot prove safety**, not a
borrow checker that rejects patterns. There are **no lifetimes in the type
system**. The only addition that changes what code is *accepted* is Stage 1
(move semantics) — and it does so only for genuinely unsound aliasing. When the
Chaperone can't prove safety (opaque FFI, unconverged recursion), it reports
and the programmer opts out via `@unsafe`/`@manual`, which already exist.
**Stay a verifier. Do not clone Rust.**
