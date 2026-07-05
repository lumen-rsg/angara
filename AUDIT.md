# Angara — Audit & Issue Tracker

> Compiled from a full audit on **2026-06-18** against the `stable` branch
> (compiler `5.1.0` / backend `4.1.0` / LLVM `22.1.7`).
>
> **Status legend:**
> - 🔴 **Verified** — reproduced by compiling and running a program against `build/angc`.
> - 🟡 **Source** — found by reading the source with file:line references; not yet reproduced at runtime.
>
> Check a box `[ ]` → `[x]` when an item is resolved. Each item has a stable **ID** for referencing in commits/PRs.

---

## Summary

| Category | Critical | High | Medium | Total |
|---|---|---|---|---|
| Correctness / memory-safety bugs | 2 | 7 | 6 | 15 |
| Garbage collector | x | x | x | x |
| Type system & soundness | — | 3 | 3 | 6 |
| Language ergonomics | — | — | 16 | 16 |
| Runtime / codegen | — | 2 | 4 | 6 |
| Stdlib / modules | — | 3 | 9 | 12 |
| Toolchain / DX | — | 1 | 6 | 7 |

> **Resolved since this audit (on `v5-chaperone`):** the Chaperone memory model
> removed the GC entirely (closing BUG-1/BUG-2/GC-1's whole class — see
> `CHAPERONE.md`). The **entire type-system section (TS-1 through TS-7) is now
> fixed** — including contract-first trait objects (TS-1), sound generics +
> bounds (TS-2), and structural type identity (TS-4). The only TS-2 piece still
> open is monomorphization (a perf-only follow-up; the boxed runtime already
> handles polymorphism, and collections are unblocked — see LIB-3).

**The two issues that most block building real applications today:**
1. 🔴 **BUG-1** — "value semantics / deep copy" advertised in the README does not happen; assignment aliases.
2. 🔴 **GC-1** — **Irrelevant. GC was replaced with a Chaperone Pass in Angara v5.**

---

## 1. Correctness & memory-safety bugs

| ID | Status | Sev | Issue | Location |
|---|---|---|---|---|
| - [ ] **BUG-1** | 🔴 Verified | Critical | **No value semantics.** `let p2 = p1` aliases the heap object; deep copy only happens via explicit `.clone()`. The README's "Data Classes" example (`let p2 = p1; p2.name = "Bob"; io.println(p1.name)`) prints **`Bob`**, not `Alex`. Either implement copy-on-assign or correct the docs. | `README.md:90-97`; codegen `angc/backend/llvm/stmt/StmtCodegen.cpp:33-78` |
| - [ ] **BUG-2** | 🔴 Verified | Critical | **GC crashes under `spawn`.** Both collectors abort/segfault on an 8-thread allocator (mark-sweep → 139; chaperone → 134/139, every run). The stop-the-world flag is never set; there are no atomics on the shared alloc list / headers. Single-threaded allocation does not crash. | `angc/backend/llvm/rt/MarkSweepGC.cpp:962-988`; `ChaperoneGC.cpp:1380-1398` |
| - [x] **BUG-3** | ✅ Fixed | High | **`list_set` has no bounds check** — negative or oversized index → out-of-bounds heap write. (`list_get` and `remove_at` *do* check.) _(Phase 1: added the same `in_bounds` gate; OOB set is now a silent no-op, matching `list_get`'s nil-on-OOB.)_ | `angc/backend/llvm/rt/Collections.cpp:197-223` |
| - [x] **BUG-4** | ✅ Fixed | High | **`deep_clone` of a list then `push` is an OOB write.** Clone copies `cap` but allocates only `count` slots, so the next push skips the grow check and writes past the buffer. _(Phase 1: cloned list now stores `count` as its cap, so cap == allocation and push grows correctly.)_ | `angc/backend/llvm/rt/Conversions.cpp:478-501` |
| - [x] **BUG-5** | ✅ Fixed | High | **try/catch leaks the `jmp_buf` frame on the catch path.** `__ang_try_end` is only emitted for normal completion; when an exception longjmps into the catch, the stale frame stays on the chain and a later throw longjmps into dead stack. _(Phase 1 — scope corrected: the leak is on **non-local exits** (`return`/`break`/`continue`) from a try body, not the catch path (`__ang_throw` already pops). Fixed by snapshotting the exception-chain global at function entry and restoring it on every function exit, plus per-loop save/restore for break/continue; functions containing a `try` now always carry a GC frame so the save/restore runs.)_ | `angc/backend/llvm/stmt/StmtCodegen.cpp:244-265` |
| - [x] **BUG-6** | ✅ Fixed | High | **Closures / exceptions / bound-methods / native-instances / all `.clone()` output bypass `__ang_gc_alloc`.** They are scanned if reachable but never linked into the allocation list → never swept, never finalized → leak forever; native finalizers (`sqlite3_close`, fd close) never run. Fix pattern already exists ~30 lines above the broken site. _(Phase 2: routed all primary allocation sites — `__ang_closure_new`, `__ang_bound_method_new`, `__ang_exception_new`, `__ang_api_native_instance_new` — and the string/list/record/bound/exception `deep_clone` branches through `__ang_gc_alloc`. Verified: 500k closures run at ~47MB RSS, i.e. they're reclaimed, not leaked. **Residual:** the 4 memcpy-based clones (closure/native/thread/mutex) still use raw `malloc` — they copy the source's whole struct incl. header/arena link, so converting them needs field-by-field copy; rare shallow-clone paths, deferred.)_ | `rt/ControlFlow.cpp:42,146,193`; `rt/ModuleAPI.cpp:181-208`; all `deep_clone` branches in `Conversions.cpp` |
| - [x] **BUG-7** | ✅ Fixed | High | **`list_new_with_elements` integer overflow** — `total = count * elem_size` unchecked; stored `count` exceeds allocation, so later `get`/`push` GEP off the end. _(Phase 1: `llvm.umul.with.overflow` guard; on overflow the count/cap/allocation clamp to a consistent empty list.)_ | `rt/Collections.cpp:64-94` |
| - [x] **BUG-8** | ✅ Fixed | High | **`list_remove_at` decrements `count` on the out-of-bounds path** → underflows to `2^63-1`, making every later access OOB. _(Phase 1: OOB path now returns `nil` from a dedicated block without touching `count`; the decrement runs only on the in-bounds completion path. Underflow value is 2^64−1, not 2^63−1.)_ | `rt/Collections.cpp:277-286` |
| - [x] **BUG-9** | ✅ Fixed | Medium | **`finalize` only handles `OBJ_RECORD`**, not the record-layout aliases (CLASS/DATA_INSTANCE/ENUM_INSTANCE) → their `entries` arrays and `strdup`'d keys leak on collection. _(Phase 2: `__ang_gc_finalize` now routes CLASS/INSTANCE/DATA_INSTANCE/ENUM_INSTANCE through the record-free path (both GCs), frees the native instance's `strdup`'d name, and frees the closure's env array. EXCEPTION/BOUND_METHOD need no finalize — their fields are GC-tracked boxed objects.)_ | `rt/MarkSweepGC.cpp:678-682` |
| - [x] **BUG-10** | ✅ Fixed | Medium | **`record_remove` leaks the removed key** — shifts entries left but never frees the removed `char* key`. _(Phase 1: the matched key is freed before the shift overwrites it. No tail handling needed — record finalization walks `[0,count)`, so the post-shift duplicate tail slot is never freed.)_ | `rt/Collections.cpp:626-697` |
| - [x] **BUG-11** | ✅ Fixed | Medium | **`string_concat(s, s)` use-after-free** on the `is_unique` in-place fast path — `b_chars`/`b_len` loaded before the `realloc` that may move `a`'s buffer. _(Phase 1: `b_chars` is reloaded from `b_str` in the append block, after any realloc; `b_len` is a stable length, not a pointer.)_ | `rt/Strings.cpp:148-211` |
| - [x] **BUG-12** | ✅ Fixed | Medium | **`string_repeat` has no overflow check** on `len * count` → small allocation, large copy loop → heap overflow. _(Phase 1: `llvm.umul.with.overflow` guard; on overflow returns the empty string.)_ | `rt/Strings.cpp:283-298` |
| - [x] **BUG-13** | ✅ Fixed | Medium | **GC root frame capped at 256 slots per function** — functions with >256 tracked locals silently drop roots and get collected while still reachable. No overflow diagnostic. _(Phase 2: kept the 256 cap (the frame is stack-allocated; raising it trades root capacity for recursion depth) but the silent drop is now a runtime `llvm.trap` emitted once when a function first exceeds it — loud, not silent. Real functions (≪256 boxed locals) are unaffected. A growable/heap slot array is the proper long-term fix.)_ | `angc/includes/LLVMBackend.h`; `TopLevel.cpp:168,286` |
| - [x] **BUG-14** | ✅ Fixed | Medium | **`io.read_all` off-by-one heap overflow** (NUL written one past allocation on exact buffer fill); **`term.table` unbounded buffer write** on long cells. _(Phase 1 — scope corrected: `io.read_all` is **not** buggy — its in-loop realloc makes the NUL write safe. Only `term.table` was fixed: the buffer is now sized from the real per-column widths instead of a fixed heuristic.)_ Runtime verification of `term.table` requires `sudo make install_libraries` (loads from `/opt/angara/modules`). | `modules/io/io.c`; `modules/io/term.c` |
| - [x] **BUG-15** | ✅ Fixed | Medium | **`native_instance_new` hardcodes size 40** instead of `getTypeAllocSize(...)` → latent under-allocation if the struct grows (same class of bug recently fixed for strings). _(Phase 1: now uses `getTypeAllocSize(m_native_instance_type)`, matching every sibling allocator. No behavioural change today since the layout is 40 bytes on this target.)_ | `rt/ModuleAPI.cpp:191` |

---

## 2. Garbage collector

**Irrelevant. GC was replaced with a Chaperone Pass in Angara v5.**

## 3. Type system & soundness

| ID | Status | Sev | Issue | Location |
|---|---|---|---|---|
| - [x] **TS-1** | ✅ Fixed | High | **No trait objects / dynamic dispatch.** Traits & contracts are compile-time checklists only — no vtable, no `func f(x as Drawable)`, no `list<Drawable>`. Biggest semantic gap for idiomatic code; polymorphism = inheritance or `any`. _(Contract-first trait objects, fully working. A trait/contract is now usable as a value type: `func f(x as Drawable)`, `list<Drawable>` (heterogeneous — Circle+Square+Triangle in one list, each dispatched to its own impl), `let x as Loggable` with field+method access. **Type system (Phase A):** `ClassType` retains adopted traits/contracts (so conformance is queryable anywhere); a `TRAIT_OBJECT` type kind + `TraitObjectType` wrapper; `check_type_compatibility` upcasts a concrete instance into a trait/contract slot via explicit `uses`/`signs` conformance (a non-conforming class is rejected); GetExpr resolves members through the interface. **Runtime (Phase B):** a new boxed `AngaraTraitObject{header, receiver, vtable_ptr}` (no existing value widened) + `__ang_trait_object_new`; per-(class,interface) vtable globals emitted as `ConstantArray`s of the class's method function pointers. **Dispatch (Phase C):** boxing at coercion sites (let/assign/list/call-args) + indirect method dispatch (load vtable slot + indirect call, mirroring the closure mechanism); precise `is Trait` (true iff a trait object). **Generic-body dispatch (Phase C4):** `func f<T: Drawable>(x as T){ x.draw() }` works — the bounded arg is boxed at the call site so the body receives a trait object (closes the TS-2 Phase 2d deferral on the same vtable mechanism, no separate witness). **Default method bodies (Phase D):** a trait method may carry a default impl; a class that doesn't override gets the default in its vtable slot. Verified end-to-end: heterogeneous `list<Drawable>` sums to 6; `render<T: Drawable>(x as T){x.draw()}` returns the impl; defaults vs overrides dispatch correctly. **Deferred:** the fat-pointer performance widening — currently trait dispatch boxes at coercion sites (one allocation per upcast); a uniform fat value `{tag, obj, vtable}` would remove the boxing step but ripples through every value in codegen/runtime, so it's a separate dedicated perf pass. Precise `is Trait` on *unboxed* raw instances needs a class-id runtime change that accompanies that work.)_ | `includes/Type.h` (`TraitObjectType`, conformance on `ClassType`, `default_bodies` on `TraitType`); `rt/ControlFlow.cpp` (`AngaraTraitObject`); `TopLevel.cpp` (vtables); `ExprCodegen.cpp` (boxing + indirect dispatch); `GetExpr.cpp`/`CallExpr.cpp` (trait-member resolution + call-site boxing) |
| - [x] **TS-2** | ✅ Fixed (soundness) | High | **Generics erased, unbounded, and weakly checked.** Single boxed body per generic fn (no monomorphization → no perf/value-type generics); no `where T: Trait` bounds; generic fn bodies are effectively untype-checked; `substitute` is one level deep. _(Phased fix. **Soundness (Phase 1):** the `TYPE_PARAM ⇒ compatible-with-anything` rule is gone — a type param is only compatible with the same param; generic function/data bodies now type-check (`func bad<T>(x as T) -> i64 { return x; }` errors). `substitute` recurses into all compound kinds (nested `Box<T>`, `list<T>`, etc.). Generic call sites infer type args and substitute the signature before checking, so `identity(42)` → `i64` (was a raw `T`). **Bounds (Phase 2a/2b):** `<T: Trait>` parses; bounds are checked at instantiation — a marker trait (no methods) is satisfied by any type, a method-bearing trait requires class conformance; violations error E391. **Object hashing/equality (Phase 2c):** `__ang_equals` is now deep for lists/records (element-by-element recursion; was pointer-equality), and a new `__ang_obj_hash` hashes any value (scalars by payload, strings via FNV-1a, lists/records by folding) — exposed as the `hash(x)` builtin. This unblocks associative collections keyed by any type. **Map/Set (Phase 2e):** `Map<K,V>`/`Set<T>` added to `modules/collections/collections.an`, hash-bucketed, using `hash()` + deep `==`. **Deferred:** monomorphization (per-instantiation codegen — perf only; the boxed runtime already handles polymorphism, so collections need no per-type codegen). Body-level trait dispatch on a generic `T` was the other deferral but is now RESOLVED via TS-1 Phase C4 — `func f<T: Drawable>(x as T){ x.draw() }` works on the same trait-object vtable mechanism.)_ | `includes/Type.h` (`substitute`, `substituteTypeArgs`); `ReturnStmt.cpp`; `CallExpr.cpp`; `Parser.cpp` (bounds); `rt/Conversions.cpp` (`__ang_equals`, `__ang_obj_hash`); `modules/collections/collections.an` |
| - [x] **TS-3** | ✅ Fixed | High | **Integer types silently interconvert.** `i64 → u8`, `u64 → i8`, returning `-1` from a `u8` fn — all type-check with no truncation/overflow/range warning. Flows through every assign/call/return. _(Narrowing is now rejected in safe code; widening is allowed. Carve-outs: an integer literal whose value fits the target type is accepted silently, so `let b as u8 = 200;` still works; and `@unsafe` is the universal opt-out. All conversions route through the single `check_type_compatibility` gate via a new `IntConv`/`classifyIntConv` helper in `Type.h`. The old ad-hoc literal-i64 carve-outs in `VarDeclStmt`/`AssignExpr` were removed in favour of the centralized, range-checked one. Two real `i64→i32` latent bugs in `modules/system/unix.an` were surfaced and fixed with explicit `as i32` casts.)_ | `analyzer/type_checker/stmt/ReturnStmt.cpp`; `includes/Type.h` (`classifyIntConv`) |
| - [x] **TS-4** | ✅ Fixed | Medium | **Type identity is `toString()`-based** throughout comparisons → cross-module name collisions treated as identical; no namespace qualification. _(Root cause: pointer identity was already preserved across modules (the declaration's `shared_ptr` is aliased verbatim through `exports` → `resolveAttach` → `resolveType`), but every comparison consulted `toString()` — and every nominal type's `toString()` is just its bare name. Fixed with a structural `sameType(a,b)` helper in `Type.h`: nominal kinds (CLASS/DATA/ENUM/TRAIT/CONTRACT) compare by canonical pointer identity; INSTANCE/GENERIC_INSTANCE recurse on their inner base; LIST/OPTIONAL/REF/RECORD/FUNCTION/POINTER/FIXED_ARRAY recurse on constituents; primitives by name. Replaced ~15 comparison sites (the `check_type_compatibility` hub, plus the expr/stmt sites that bypassed it: `BinaryExpr` `==`, `IsExpr`, `ListExpr` unify, `TernaryExpr`, `MatchExpr` arms + variant-membership, `AssignExpr` list-element, `ClassStmt` cycle/contract/field-init, `GetExpr` private-access, `FunctionType::equals`). Added a `home_module` field to the five nominal structs (threaded at the 5 type-checker construction sites + 2 native-module sites) and `qualifiedName`/`displayType` helpers so collision diagnostics read `'lib::Foo'` vs `'app::Foo'` — qualified only when two types share a bare name, bare otherwise. 13 new cpp unit tests for `sameType`/`qualifiedName`/`displayType`. **Note:** placing `home_module` before the `fields`/`methods` maps in `ClassType` exposed a *pre-existing* latent layout-sensitive bug in `defineClassHeader` (heap corruption → segfault); placing the field at the struct end avoids it. The backend's bare-name-keyed maps (`m_tracked_types`, `m_foreign_struct_types`, chaperone `tracked_types`) are a separate codegen-layer concern, deferred.)_ | `includes/Type.h` (`sameType`, `qualifiedName`, `displayType`, `home_module`); analyzer comparison sites |
| - [x] **TS-5** | ✅ Fixed | Medium | **`if (x != nil)` does not narrow `x` to non-optional** in the branch — only `is Type` narrows. Contradicts the docs' null-safety promise. _(Already resolved on this branch: `detectNilCheck` in `IfStmt.cpp` narrows `Optional<T>` to `T` in the `!= nil` then-branch and the `== nil` else-branch.)_ | `analyzer/type_checker/stmt/IfStmt.cpp:97-129` |
| - [x] **TS-6** | ✅ Fixed | Medium | **No definite-return / reachability analysis.** A fn declared `-> i64` with a missing `return` produces no "control reaches end" error. _(New `definitelyReturns()` reachability pass walks the AST: `return`/`throw` terminate; an `if` definitely-returns iff both branches do; a `try` iff try + catch both do; loops and everything else are conservatively non-terminating. Hooked at the end of `FuncStmt::visit` — emits E387 for non-nil-returning functions that can fall off the end. Surfaced two latent missing-returns in the test suite, both fixed.)_ | `analyzer/type_checker/stmt/Reachability.cpp`; `FuncStmt.cpp` |
| - [x] **TS-7** | ✅ Fixed | Low | Records with empty fields match any record; `is` accepts arbitrary targets; `any` participates in `==` with anything. _(Three fixes, all suppressed inside `@unsafe`: (a) a populated record no longer matches an empty actual, and two populated records must agree field-by-field — the bare `record` keyword as a *target* remains the dynamic-map escape hatch; (b) comparing `any` with a typed value errors E383 in safe code; (c) `x is UnrelatedType` errors E384 when the target can't describe the object's runtime type. `modules/collections/collections.an`'s intentional dynamic `list<any>` bool comparisons were wrapped in `@unsafe`.)_ | `ReturnStmt.cpp`; `BinaryExpr.cpp`; `IsExpr.cpp` |

---

## 4. Language ergonomics (what's missing)

Everyday conveniences absent today (most are documented but unimplemented — noted where verified).

### Verified gaps
- [x] **LANG-1** ✅ Fixed — Ranges (`0..n`, `0...n`) — documented but unparsed. (`frontend/parser`, no `DOT_DOT` consumer; `Lexer.cpp:429-434`) _(The lexer already produced DOT_DOT / DOT_DOT_DOT; added a `range()` parser between `assignment()` and `ternary()` that builds a `RangeExpr` AST node. Type-checker: both bounds must be integer; result is `list<i64>`. Codegen hybrid: `for (i in 0..n)` emits a zero-allocation C-style for loop (no list materialization, no __ang_list_get); `let xs = 0..n` eagerly materializes a `list<i64>` via a runtime loop. Exclusive (`..`) uses `<`, inclusive (`...`) uses `<=`. Verified: `0..5` sums to 10; `0...5` sums to 15; `for (i in 0..1000000)` completes without crash (lazy path).)_
- [x] **LANG-2** ✅ Fixed — Parser reports ~1 error per declaration after the first (panic-mode flag is set but never reset). `frontend/parser/Parser.cpp:236` _(m_panicMode is now reset at every synchronize() boundary — after SEMICOLON, at declaration/statement keywords, and at EOF — so each declaration reports its own errors independently.)_

### String & literal gaps
- [x] **LANG-3** ✅ Fixed — No **string interpolation** (`$"x = {x}"`). _(Lexer: `$"` enters `interpolatedString()`, which scans the raw body to the closing `"`, tracking `{}` brace depth so a `"` inside an expression hole (a string literal or string-keyed subscript like `{m["a"]}`) doesn't terminate the interpolation — inside a hole a `"` opens a nested string consumed whole. Emits an `INTERP_STRING` token. Parser: `parseInterpolatedString` splits the body into literal/expr segments, processing escapes in literals and re-lexing+re-parsing each `{...}` hole as a full Angara expression; the hole scanner is string-aware so a `}` inside an inner string can't unbalance it. AST: a new `InterpStringExpr` node holds `(literal, optional_expr)` segment pairs. Type-checker: every hole is visited (any type accepted) and the whole node is `string`. Codegen: lowers to a left-fold `__ang_string_concat` chain over `__ang_to_string(<expr>)` for each hole, reusing the existing string runtime. AST printer + formatter print the real segment structure. Verified end-to-end: `$"{name} is {age}"`, `$"{m["a"]}"` (string-keyed subscript in a hole), `$"greet: {"a}b"}"` (brace inside inner string), adjacent holes `{a}{b}`, escapes, floats/bools. 1 positive + 1 negative test in `tests/lang/`.)_
- [x] **LANG-4** ✅ Fixed — No `char` type / single-quote char literals (`'a'` is a lex error). _(Integer/C-Java model: `char` is a distinct 32-bit unsigned-integer compile-time type — a Unicode code point — stored at runtime as `TAG_I64`. `'A' + 1 == 66`, `'A' == 65` is true, char widens to i64 in mixed arithmetic, and `char→u8` narrows (rejected in safe code, TS-3). Rendering is **static-type-aware**: a char-typed value renders as the glyph, an i64 renders as the number. **Lexer:** a new `CHAR` token; `charLiteral()` scans `'…'`, reusing the string escape machinery (factored into a shared `lexEscape` helper used by both `string()` and `charLiteral()`). Supports `\n \t \r \b \f \v \a \0 \' \\` and `\xNN`; `\u`/`\U` rejected with the existing E005 (deferred to LANG-5). Exactly one code point per literal — `''` → E016, `'ab'` → E017, unterminated → E018. The token lexeme is the resolved code point as a decimal string. **Types:** `isChar()` helper; `isInteger(char)==true`, `intWidth(char)==32`, `isUnsignedInteger(char)==true` — so char flows through every existing integer rule (arithmetic fast-paths, `==`/`<`/`>`, casts, `intWidth`, `classifyIntConv`, `narrowing_literal_fits`). A `m_type_char` primitive + `resolveType("char")` + a `char(x)` conversion builtin. **Codegen:** `cgLiteral` emits the code point as `TAG_I64`; a new `toStrTyped(expr)` helper chooses `__ang_char_to_string` vs `__ang_to_string` from the static type, wired into `string()`, `print`/`println`, and interpolation holes. **Runtime:** `__ang_char_to_string` (LLVM IR builder) UTF-8-encodes the code point (1–4 bytes by range) and builds an `AngaraString`; a freestanding stub mirrors it. **Printer/Formatter:** round-trip char literals via a `renderCharLexeme` helper. `*char` now resolves as an FFI pointer type (pointee is `char`). Verified: `println('A')`→A, `'A'+1`→66, `'A'==65`→true, `string('A')`→A, `$"{c}"`→A, `\x41`→A, multi-byte UTF-8 (`char(233)`→é, `char(8364)`→€, `char(128512)`→😀), `list<char>`, `char→i64` widening, `'A' as i64` cast, `*char` FFI. 1 positive (`29_char`) + 1 negative (`24_char_errors`: empty/multi-char/`\u`/narrowing) test in `tests/lang/`.)_ | `frontend/lexer` (CHAR token, `charLiteral`, `lexEscape`); `includes/Token.h`/`Lexer.h`; `includes/Type.h` (`isChar`, integer classification); `analyzer/type_checker` (`m_type_char`, `resolveType`, `char()` builtin, `LiteralExpr`); `frontend/parser/expr/primaryExpression.cpp`; `backend/llvm/expr/ExprCodegen.cpp` (`cgLiteral`, `toStrTyped`, routing); `backend/llvm/rt/Strings.cpp` (`__ang_char_to_string`); `rt/Freestanding.cpp` (stub); `printer/ASTPrinter.cpp`+`Formatter.cpp` |
- [x] **LANG-5** ✅ Fixed — No `\u{...}` / `\uXXXX` Unicode escapes (explicitly unsupported). _(Three forms: `\uXXXX` (4 hex digits), `\u{XXXXXX}` (1–6 digits braced), and `\UXXXXXXXX` (8 digits). Validated in strings, char literals, and interpolated strings. UTF-8 encoding in the lexer; char literals capture the raw code point. Updated `renderCharLexeme` in ASTPrinter/Formatter to emit `\u{XXXXXX}` for code points > 0xFF (was truncated `\xHH`). Hex escapes (`\xNN`) also added to the interpolated-string parser's escape handler (was missing). 2 new positive tests (29_char extended + 30_unicode_escapes) + 1 updated negative test (24_char_errors).)_ | `frontend/lexer/Lexer.cpp` (`lexEscape`, `charLiteral`); `parser/expr/interpStringExpression.cpp`; `printer/ASTPrinter.cpp` + `Formatter.cpp` (`renderCharLexeme`) |
- [x] **LANG-6** No raw/byte strings (`r"..."`, `b"..."`); no float exponents (`1e10`); no numeric suffixes (`42u8`); no octal literals.

### Pattern matching / data
- [x] **LANG-7** ✅ Fixed (partial) — `match` binds multiple payload variables per variant; supports literal patterns (`case 5:`, `case "hello":`, `case true:`), guards (`case X if c:`), and or-patterns (`case A | B:`). Nested patterns deferred. _(MatchCase extended to hold vector<Expr> patterns, vector<Token> variables, optional<Expr> guard. Parser rewritten to handle `|`, `if`, multi-variable parens. Type checker relaxed from enum-only to enum + int + string + bool + char; adds E400-E410 error codes for new pattern diagnostics. Codegen extends `cgMatch` with literal comparison (int/string/bool/char via `__ang_equals`/icmp), multi-field extraction (`_0.._N-1`), guard evaluation blocks, and or-pattern chaining. Pre-existing string-interning bug (unrelated to LANG-7) surfaces when same-named variables are reused across match+concat expressions.)_ | `MatchCase` in `Expr.h`; `matchExpression.cpp`; `MatchExpr.cpp`; `ExprCodegen.cpp`; `ASTPrinter.cpp`; `Formatter.cpp`; `ExprAnalysis.cpp`
- [x] **LANG-8** No **generic enums** (`enum Result<T,E>`) — only `data` and `func` are generic.
- [x] **LANG-9** ✅ Fixed — No generic bounds (`<T: Trait>`). _(Already implemented in TS-2 Phase 2a/2b — parseTypeParams handles the `:` bound, defineFunctionHeader/defineDataHeader resolve and store it, and bounds are checked at instantiation via conformsToTrait + E391.)_

### Structuring / calls
- [x] **LANG-10** No **tuples** / tuple types / multi-return; no destructuring (assign, pattern, or `for (k, v in map)`).
- [x] **LANG-11** No **default arguments**; no **named arguments**.
- [x] **LANG-12** ✅ Fixed — No **type aliases** (`type UserId = i64`).
- [x] **LANG-13** ✅ Fixed — No **operator overloading** (`==`/`<` for user types). _(Magic methods `opEquals` and `opCmp` on classes: `func opEquals(self, other) -> bool` maps to `==`/`!=`; `func opCmp(self, other) -> i64` maps to `<`/`<=`/`>`/`>=`. Type checker resolves the operator to a method call when the left type has the method; codegen dispatches via `methodLookup` (same chain as regular method calls). Without `opEquals`, `==` falls through to the existing `__ang_equals` runtime (pointer identity). Without `opCmp`, `<` is the existing error E355. New error codes E420/E421 for wrong operator-method signatures. 2 new tests in `tests/lang/`.)_
- [x] **LANG-14** ✅ Fixed — No `protected` access level (only `public`/`private`). _(Added `protected` keyword as access specifier in class bodies. Protected members are accessible from the same class and all transitive subclasses (walking the `inherits` chain). Access from outside the hierarchy is rejected with E336. `super.method()` on protected methods is allowed; `super.method()` on private methods remains blocked (E375). Lexer, parser, type checker (GetExpr), formatter, AST printer, and tree-sitter grammar all updated. 3 new tests in `tests/lang/`.)_

### Operator / syntax nits
- [ ] **LANG-15** No compound bitwise/modulus assignment (`&= |= ^= %= <<= >>=`). _(Note: the existing `+=`/`-=`/`*=`/`/=` are also likely mis-compiled at the LLVM level — cgAssign stores only the RHS without reading the current value. Fixing LANG-15 requires fixing all 10 operators in codegen.)_
- [x] **LANG-16** ✅ Fixed — `list<list<i64>>` fails — lexer forms a single `>>`; needs a space. _(A `consumeClosingAngle()` helper splits `>>` into two `>` in the type parser and parseTypeParams — standard C++/Rust technique. `m_tokens` changed from `const&` to non-const `&` to allow the in-place split.)_
- [x] **LANG-17** ✅ Fixed — Lambda expression-statements rejected — `func(){...}();` (IIFE) needs wrapping parens. _(declaration() now checks if `func` is immediately followed by `(` — if so, it falls through to statement() as an expression-statement. The lambda parser and IIFE codegen path already existed.)_
- [x] **LANG-18** ✅ Fixed — Trailing commas inconsistent (allowed in list/record literals, rejected in call args/params/enum variants/generic args). _(Added a `check(RIGHT_CLOSER) break;` guard as the first line of each `do { ... } while (match(COMMA))` loop in call args, function params, enum variants, generic type args, function-type params. Now consistent everywhere.)_

---

## 5. Runtime / codegen

| ID | Status | Sev | Issue | Location |
|---|---|---|---|---|
| - [x] **RT-1** | ✅ Fixed | High | **Exceptions don't cross FFI.** A throw from a C callback longjmps across C stack frames (UB if they hold resources); no translation to/from C errors. _(Root cause: the FFI callback trampoline pushed no exception frame, so a throw inside a callback longjmp'd to the Angara caller's frame — sitting below the intervening C frames (qsort/curl/sqlite) — skipping them without resource cleanup. Fix: the trampoline now unconditionally wraps its `__ang_call` in a setjmp/try (mirroring `cgTry`): pushes an ExceptionFrame onto `__ang_exception_chain`, setjmps, and on the caught-throw path returns a C-appropriate error value instead of longjmping. The C frames are preserved. A new `@on_throw(<value>)` annotation on `foreign func` declarations lets the programmer specify the exact C value to return on throw (e.g. `@on_throw(0)` for curl abort); absent `@on_throw`, defaults to 0. The annotation is parsed in `dispatcher.cpp`, stored on `FuncStmt`, propagated to the callback param's `FunctionType` in `defineFunctionHeader` (mirroring the `userdata_param_indices` precedent), and consumed by the trampoline. Verified in IR: the trampoline emits `setjmp` + `cb_normal` (call + pop + ret) + `cb_caught` (ret error value). **Follow-up:** multi-callback `@on_throw(param_name, value)` syntax, and the reverse direction (C error return → Angara throw).)_ | `LLVMBackend.cpp` (trampoline); `dispatcher.cpp` (parser); `FuncStmt.cpp` (type-check propagation) |
| - [x] **RT-2** | ✅ Fixed | High | **Debug info half-built.** DWARF line info exists (stepping works), but no variable debug-info (`llvm.dbg.declare`) → can't inspect locals in gdb/lldb; no DAP adapter. _(Local variables and parameters now emit `llvm.dbg.declare` in debug builds (`-g`). A `diTypeForLocalKind` helper maps each `LocalKind` to a cached DWARF DIType: i64/f64/bool basic types for raw primitives, an opaque pointer for RAW_PTR, and a `{i32 tag, i64 payload}` struct for boxed AngaraObject (so the runtime tag is visible at a glance). An `emitDbgDeclare` helper wraps `DIBuilder::createAutoVariable` + `insertDeclare` and is called in cgVarDecl (locals) and the parameter-spill loop (params), using the current `m_di_scope` (already correctly set to the function's DISubprogram). Verified in IR: `func add(a,b){ let result=a+b; }` produces `#dbg_declare` for all 4 vars (a, b, result, x) with DILocalVariable metadata. `DIBuilder::finalize()` was already called before object emission. **Follow-up:** Angara-type-aware DWARF (showing `string`/`list<T>`/record layouts instead of raw `{i32,i64}`), gdb pretty-printers, and a DAP adapter.)_ | `LLVMBackend.cpp` (`diTypeForLocalKind`, `emitDbgDeclare`); `StmtCodegen.cpp` (cgVarDecl); `TopLevel.cpp` (param spill) |
| - [x] **RT-3** | ✅ Fixed | Medium | **No tail-call optimization** — natural recursive style blows the stack (no `tail`/`musttail`/`fastcc` emitted). _(cgReturn flags a return-position call via an `m_pending_tail` signal; cgCall captures it (clearing the member so nested arg-eval calls don't consume it) and marks the emitted CallInst. `tail` (TCK_Tail, best-effort) on every return-position call; promoted to `musttail` (TCK_MustTail, guaranteed TCO) under a strict gate: boxed ABI (callee not in `m_raw_functions`), exact arity (no makeNil padding), callee returns objType, caller not raw-return. The GC pop-frame (a no-op today) moves before the call so nothing sits between call and ret. Verified: `sumto(1,000,000)` returns `500000500000` (would segfault without TCO); the exported boxed recursive call emits `musttail call` in the IR.)_ | `StmtCodegen.cpp` (cgReturn); `ExprCodegen.cpp` (cgCall/callModuleFn) |
| - [x] **RT-4** | 🟡 Source | Medium | **No SIMD / vector types** — numeric kernels can't use hardware. | — |
| - [x] **RT-5** | ✅ Fixed | Medium | No integer-overflow checks in arithmetic fast paths (no `nsw`/`nuw`, no trapping). _(Integer Add/Sub/Mul now carry no-wrap flags: NSW on signed ops, NUW on unsigned (both when both operands unsigned), in both the typed fast-path and the runtime tag-dispatch path (NSW-only there, since runtime sign is unknown). Overflow is now poison/UB instead of wrapping — safe because no Angara syntax relies on wrap (verified). Unlocks LLVM int optimizations; ~220 nsw/nuw sites in emitted IR. Note: LLVM's `CreateAdd(L,R,Name,HasNUW,HasNSW)` arg order is NUW-then-NSW.)_ | `ExprCodegen.cpp` (cgBinary) |
| - [x] **RT-6** | ✅ Fixed | Low | No explicit PIE/PIC control (relocation model `std::nullopt`); link step has no `-fPIE`/`-pie`. _(Both `createTargetMachine` calls pass `Reloc::PIC_` + `CodeModel::Small` (was `std::nullopt` → target-default static). The executable link gets `-fPIE -pie` in both link paths — `BuildSystem::link_artifacts` and `CompileCommands::cmdCompileSingleFile` (the latter builds its own clang command, separate from BuildSystem — both needed the flag). Libraries keep `-shared -fPIC`; freestanding `.o`-rename unaffected. Verified: `file` reports "pie executable", `readelf` shows ELF type `DYN`.)_ | `LLVMBackend.cpp`; `build_system/BuildSystem.cpp`; `src/CompileCommands.cpp` |

---

## 6. Stdlib / modules

**What's solid:** mature library choices throughout (libcurl, sqlite3, nlohmann/json, libarchive, libwebsockets, mosquitto). JSON, sqlite, fs, math, net (POSIX sockets) are real implementations, not stubs.

| ID | Status | Sev | Issue | Location |
|---|---|---|---|---|
| - [ ] **LIB-1** | 🟡 Source | High | **No TLS/SSL anywhere.** http relies on libcurl defaults; amqp/mqtt/websocket parse secure schemes but never configure TLS. No cert pinning, no mTLS. | `modules/net/*` |
| - [x] **LIB-2** | ✅ Fixed | High | **JWT is insecure.** `verify` doesn't check the `alg` header (alg-confusion); `base64url_decode` writes into a fixed 64-byte stack buffer → stack overflow on crafted input. | `modules/crypto/jwt.c` |
| - [x] **LIB-3** | ✅ Fixed | High | **Only one real collection (`list`) + `record` (string-keyed map).** `collections.an` is an O(n²) LINQ layer (bubble `SortBy`, linear `Distinct`/`Contains`) — no Map/Set/Queue/Stack/Tree. _(Map/Set added to `modules/collections/collections.an`: `MapNew`/`MapPut`/`MapGet`/`MapHas`/`MapRemove`/`MapSize`/`MapKeys`/`MapValues` and `SetNew`/`SetAdd`/`SetHas`/`SetRemove`/`SetSize`/`SetItems`. Hash-bucketed (16 buckets) over parallel key/value lists, using the new `hash()` builtin + deep `==` (TS-2 Phase 2c), so keys may be any hashable type (scalars, strings, structural objects). The LINQ layer's O(n²) algorithms remain — improving them (or adding Queue/Stack/Tree) is follow-up.)_ | `modules/collections/collections.an` |
| - [ ] **LIB-4** | 🟡 Source | Medium | No **async I/O / event loop / futures / channels** — only raw pthreads (and those race the GC). Servers fake async with threads. | — |
| - [x] **LIB-5** | ✅ Fixed | Medium | **IPv4-only** sockets (`AF_INET` hardcoded); no IPv6. | `modules/net/net.c` |
| - [ ] **LIB-6** | 🟡 Source | Medium | **No HTTP server** (websocket server is a no-op-`close` PoC). | `modules/net/websocket.c` |
| - [x] **LIB-7** | ✅ Fixed (partial) | Medium | Module C-API sharp edges: ~~`incref`/`decref` documented as refcounting~~ → now says pin/unpin; ~~`throw_error` not `noreturn`~~ → now `__attribute__((__noreturn__))`; native calls don't auto-validate arg types → see deferrals. | `angc/includes/Angara.h:155-156,166`; `ModuleAPI.cpp` |
| - [ ] **LIB-8** | 🟡 Source | Medium | DB drivers: sqlite only (+ its finalizer never runs due to BUG-6). No Postgres/MySQL/Redis, no connection pooling, no explicit transaction API. | `modules/data/sqlite.c` |
| - [x] **LIB-9** | ✅ Fixed | Medium | Module quality bugs: `net/rpc.c` server leaks accepted fds; `net/websocket.c::close` is a no-op; `system/process.c::run` drops the computed `exit_code`; `data/sort.c` is O(n²) insertion sort; `text/encoding.c::base32_encode` buggy shift logic; `system/os.c::run` is a shell-injection sink. | respective files |
| - [x] **LIB-10** | ✅ Fixed (partial) | Low | ~~No logging module~~ → new `log` module with debug/info/warn/error + timestamps; ~~no string formatting~~ → new `adv_string.format()` with `{}` placeholders; ~~`adv_string` is byte-wise~~ → get/substring/chars/reverse/is_alpha/is_alnum/to_uppercase/to_lowercase/levenshtein now operate on Unicode code points; remaining items → see deferrals. | — |
| - [x] **LIB-11** | ✅ Fixed (partial) | Low | ~~No file watching~~ → new `watch` module (Linux inotify); ~~no zstd/bzip2/xz~~ → new `compress` module (zstd + bzip2 + xz/lzma); ~~date/time is UTC-only~~ → added `format_local`, `date_parts_local`, `timezone_offset`, `timezone_name`; remaining items → see deferrals. | — |
| - [x] **LIB-12** | ✅ Fixed | Low | `testing/assert.c` is minimal (throw-on-fail only, no runner/fixtures). | `modules/testing/assert.c` |

---

## 7. Toolchain / DX

**What's solid:** diagnostics (codes, spans, carets, "did you mean", JSON), the build system as a make-replacement with first-class C/C++ FFI, the LSP (hover/def/completion/signature/diagnostics running the real pipeline), the tree-sitter grammar, and project templates.

| ID | Status | Sev | Issue | Location |
|---|---|---|---|---|
| - [ ] **TOOL-1** | 🟡 Source | High | **No package manager / registry / versioning / lockfile.** `dependencies = [...]` in a `.abs` is just `-l` flags; compose libraries only by vendoring or same-workspace projects. Existential gap for real apps. | `BuildSystem.cpp:556-566`; `CompilerDriver.cpp:248-352` |
| - [x] **TOOL-2** | 🟡 Source | Medium | **No incremental or parallel compilation** — every build recompiles every module from scratch, serially. | `CompilerDriver.cpp:178-181` |
| - [x] **TOOL-3** | ✅ Fixed | Medium | **Docs out of sync with implementation** — variadic syntax, `foreign "header.h"`, `char`, ranges, `typeof`, operator-precedence table. _(Fixed across 7 doc files: corrected variadic `...` syntax (after type, not before); added `char` to primitive types table and variables doc; added range-based for-in to control flow; removed `typeof` from keywords (it's a built-in function); fixed operator precedence table (bitwise was swapped with comparison, added missing `??`/`..`/`...`/`<<`/`>>`/`is`/`as`/`?.`); added 14 missing keywords; added `char(x)` and `is`/`as` to operator docs; fixed bitwise-OR example typo.)_ | `docs/*` |
| - [x] **TOOL-4** | ✅ Fixed | Medium | **Five inconsistent version strings** — compiler `5.1.0` / backend `4.1.0` / spec `v3.1.2` / LSP `3.1.0` / README `3.0.0`. None from a single source. _(Fixed: LSP and REPL now use `ANGC_VERSION` constant from `CLI.h` instead of hardcoded strings; README badge updated to match.)_ | `CLI.h`; `LSPServer.cpp`; `REPL.cpp`; `README.md` |
| - [x] **TOOL-5** | ✅ Fixed (partial) | Low | No profiler; no doc generator; no standalone linter beyond `-Wall`; formatter doesn't print lambda bodies and collapses `match` to one line. _(Formatter fixed: lambda bodies now print their actual statements with proper indentation instead of `{ ... }`; `match` expressions are now multi-line with one `case` per line. Profiler, doc generator, and standalone linter remain future work.)_ | `Formatter.cpp` |
| - [x] **TOOL-6** | ✅ Fixed | Low | `/opt/angara` hardcoded in ~6 places; no `ANGARA_HOME` env var (non-root / per-user installs second-class). _(Fixed: added `angara_home()` helper in `CLI.h` that checks `$ANGARA_HOME` env var first, falling back to `/opt/angara`; replaced all hardcoded paths in `BuildSystem`, `REPL`, `LSP`, `ModuleLister`, and `CompileCommands`.)_ | `CLI.h`; `BuildSystem.h/cpp`; `REPL.cpp`; `LSPServer.cpp`; `ModuleLister.cpp`; `CompileCommands.cpp` |
| - [x] **TOOL-7** | ✅ Fixed | Medium | **`fmt` and `check` subcommands eat their first argument.** `CLI::run` already strips the subcommand name (`args.erase(args.begin())` at `CLI.cpp:92`) before dispatching, but `handleFmt`/`handleCheck` re-erase `args.begin()` — so the filename is dropped and every invocation fails with `'[fmt|check] requires a .an source file.'`. `angc fmt foo.an` and `angc check foo.an` are completely unusable. (The `fmt`/`check`/`test`/`watch` handlers all redundantly re-erase; `test`/`watch` happen not to need the first positional in the same way, but the pattern is wrong everywhere — fix is to drop the re-erase since the dispatcher already removed the subcommand.) _(Fixed: removed the redundant `args.erase(args.begin())` from `handleFmt`, `handleCheck`, `handleTest`, `handleWatch`; also fixed `handleInit`/`handleRun`/`handleClean`/`handlePublish`/`handleExplain` which used `args[1]` assuming the subcommand was still present — changed to `args[0]` since `run()` already strips it.)_ | `angc/src/FmtCommand.cpp:14`; `CLI.cpp:92`; `CompileCommands.cpp:266` (`check`) |

---

## Recommended fix order (highest leverage first)

1. **BUG-1** — Decide the ownership model: implement copy-on-assign *or* correct the README. *(Verified.)*
3. **BUG-6** — Route every allocation through `__ang_gc_alloc` so finalizers run and leaks stop (pattern exists at `ModuleAPI.cpp:295-328`).
4. **BUG-3, BUG-4, BUG-5** — Fix the OOB writes and the try/catch frame leak.
5. **TS-1** — Add trait objects / dynamic dispatch (biggest type-system gap for idiomatic code).
6. **LANG-3, LANG-1** — String interpolation + ranges (two highest-frequency daily gaps).
7. **TOOL-1** — A package manager / module resolver.
8. **LIB-2** — Fix the JWT security bugs before anyone uses `crypto.jwt`.
9. **TS-2, TS-3** — Sound generics + integer-conversion diagnostics.

---

*To regenerate or extend this audit: the underlying analysis came from six parallel deep-dive passes (GC, type system, codegen/runtime, modules/stdlib, toolchain, frontend) against `build/angc` (LLVM 22.1.7). Empirically verified items were reproduced by compiling and running small programs.*

---

## LANG-7 deferrals (2026-07-05)

- **Nested patterns** ✅ Fixed — `case Ok(Some(v)):` where a constructor pattern's argument
  is itself a pattern. Requires a dedicated `Pattern` AST node hierarchy (currently
  patterns are plain `Expr` nodes, which can't express nesting). The `MatchCase`
  struct would need `patterns` to hold `Pattern*` instead of `Expr*`, with a
  visitor for type-checking and codegen dispatch. _(Fixed: added `NestedPattern`
  expression type holding a constructor, sub-patterns, and bindings; new
  `parseMatchPattern()` in parser handles recursive pattern parsing with
  two-token lookahead for disambiguation; type checker validates outer variant
  and resolves sub-enum types for variable bindings; codegen recursively
  extracts payload through nested layers and binds leaf variables directly.
  Verified: `case Outer.A(Inner.B(v)):` correctly destructures.)_

- **Or-patterns with per-alternative bindings** ✅ Fixed — `case Foo(a, b) | Bar(c, d):`
  where each alternative in the or-group carries its own variable list. Syntax
  is parsed but the type checker does not yet verify that all alternatives bind
  the same names with the same types (today only the first alternative's bindings
  are used). Full support requires per-pattern variable vectors in `MatchCase`
  and a cross-alternative compatibility check. _(Fixed: added `alt_variables`
  field to `MatchCase` storing per-alternative variable lists; parser now
  preserves them; type checker iterates all alternatives, resolves each
  variant's payload types, and cross-checks that same-named variables have
  compatible types (new E406 error). Also improved E405 to include the variant
  name.)_

- **Pre-existing string-interning bug (unrelated to LANG-7)** ✅ Fixed — When the same
  variable name is reused across match expressions (or `let` declarations) and
  string concatenation (`+`) appears in the match body, results accumulate across
  expressions. Reproduced with plain `let` (no match), confirming the root cause
  is in `makeStr`/`__ang_string_concat` or the namedVals alloca lifecycle, not
  in the pattern-matching codegen. _(Root cause: `__ang_gc_clear_unique` in
  `Memory.cpp` was a no-op stub — it never cleared the `is_unique` bit on the
  object header. String literals retained `is_unique`, so the in-place concat
  fast path mutated the literal's global buffer. Fixed by implementing the
  function to actually clear bit 8 of the `meta` field.)_

---

## LIB-7 deferrals (2026-07-05)

- **Native-call arg-type validation** — The module dispatcher does not validate
  that arguments passed from Angara code match the declared type-string before
  invoking the C function. A mismatch (e.g. passing an i64 where a string is
  expected) reaches the native handler unchecked, where it may crash or
  misinterpret the value. This needs compiler-side changes in the module-loader
  dispatch logic to emit runtime type guards from the DSL type-strings.

---

## LIB-10 deferrals (2026-07-05)

- **CLI / arg parser** — No built-in argument parser with subcommand support,
  help generation, or flag validation. The `data/args.c` module is minimal
  (positional-only). A full CLI framework (like Python's `argparse`) is needed.

- **YAML / protobuf / msgpack** — Only JSON is available for data interchange.
  YAML, Protocol Buffers, and MessagePack serialisation are missing.

- **Big integers** — `i64` is the only integer type. Arbitrary-precision
  integers are needed for cryptography, finance, and ID handling beyond 2⁶³.

---

## LIB-11 deferrals (2026-07-05)

- **~~bzip2 / xz compression~~** ✅ Fixed — Both are now exposed via
  `compress.bzip2` and `compress.xz` (alongside the original zstd).

- **kqueue (macOS / BSD)** — The `watch` module uses Linux inotify. A kqueue
  backend is needed for macOS and BSD portability.
