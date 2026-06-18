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
| Garbage collector | 1 | 5 | 2 | 8 |
| Type system & soundness | — | 3 | 3 | 6 |
| Language ergonomics | — | — | 16 | 16 |
| Runtime / codegen | — | 2 | 4 | 6 |
| Stdlib / modules | — | 3 | 9 | 12 |
| Toolchain / DX | — | 1 | 5 | 6 |

**The two issues that most block building real applications today:**
1. 🔴 **BUG-1** — "value semantics / deep copy" advertised in the README does not happen; assignment aliases.
2. 🔴 **GC-1** — the GC crashes (SIGSEGV/SIGABRT) under the language's own `spawn`; multithreaded Angara is currently unsound.

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

The precise, type-directed root discovery and tracing is **genuinely well done** and is the right design — this is the part to keep and build on. The problems are all in concurrency, the moving-GC barrier story, and bounds.

| ID | Status | Sev | Issue | Location |
|---|---|---|---|---|
| - [ ] **GC-1** | 🔴 Verified | Critical | **No synchronization across threads.** STW flag never set; safepoints never emitted; zero atomics on shared heap/list/header state. → crashes under `spawn` (see BUG-2). | `MarkSweepGC.cpp:962-988`; `ChaperoneGC.cpp:1380-1398` |
| - [ ] **GC-2** | 🟡 Source | High | **ChaperoneGC relocation is unsound** — `__ang_gc_read_barrier` exists and is correct but is **never emitted into generated code**, so moved objects leave dangling pointers. Either emit the barrier or revert ChaperoneGC to non-moving arena mark-sweep. | `ChaperoneGC.cpp:1662-1698`; no call sites in `LLVMBackend.cpp`/`ExprCodegen.cpp` |
| - [ ] **GC-3** | 🟡 Source | High | **ChaperoneGC arena pool fixed at 256 with no bounds check** → out-of-bounds global write at ~256 MB live heap; 8-bit `arena_id` wraps. | `ChaperoneGC.cpp:589-607` |
| - [ ] **GC-4** | 🟡 Source | High | **`__ang_gc_obj_size` hardcodes sizes (32/48/64/80)** that disagree with real `getTypeAllocSize` → arena walks/compaction step by the wrong stride for many objects. | `ChaperoneGC.cpp:1737-1760` |
| - [ ] **GC-5** | 🟡 Source | High | **"Concurrent/annealing" chaperone is dead code.** `chaperone_spawn` never calls `pthread_create` (just sets a flag); compaction runs synchronously, relocates one object per cycle; energy function misclassifies liveness between collections. Replace with a plain sliding compactor if defragmentation is the goal. | `ChaperoneGC.cpp:2476-2498`, `1762-2419` |
| - [ ] **GC-6** | 🟡 Source | Medium | **Recursive marking with no explicit mark stack** → deep object graphs (~10k+ depth) exhaust the native stack. _(Phase 2 status: deferred — the recursive mark→scan→mark is sound, just stack-hungry on pathological depth; converting to an explicit worklist is a worthwhile but invasive restructure of the chaperone (active) + mark-sweep mark funcs, better as a focused follow-up than bundled with the soundness pass.)_ | `MarkSweepGC.cpp:365-411` |
| - [ ] **GC-7** | 🟡 Source | Medium | **`count` reset to 0 on collect, not to survivors** → collection frequency wrong for stable working sets (collects after N *fresh* allocations, not N live objects). _(Phase 2 status: reviewed, judged **not a bug** — resetting the alloc counter to 0 post-collection is the standard "collect every N allocations" trigger policy; "reset to survivors" is non-standard and wouldn't fix the stated symptom either. Left as-is.)_ | `MarkSweepGC.cpp:985`; `ChaperoneGC.cpp:1397` |
| - [ ] **GC-8** | 🟡 Source | Low | No weak references; no user-visible finalizers; no finalizer ordering/resurrection. (Acceptable for a systems GC, noted for completeness.) | — |

---

## 3. Type system & soundness

| ID | Status | Sev | Issue | Location |
|---|---|---|---|---|
| - [ ] **TS-1** | 🟡 Source | High | **No trait objects / dynamic dispatch.** Traits & contracts are compile-time checklists only — no vtable, no `func f(x as Drawable)`, no `list<Drawable>`. Biggest semantic gap for idiomatic code; polymorphism = inheritance or `any`. | `analyzer/type_checker/stmt/ClassStmt.cpp:139-167` |
| - [ ] **TS-2** | 🟡 Source | High | **Generics erased, unbounded, and weakly checked.** Single boxed body per generic fn (no monomorphization → no perf/value-type generics); no `where T: Trait` bounds; generic fn bodies are effectively untype-checked; `substitute` is one level deep. | `Type.h:437-445`; `TopLevel.cpp:66`; `ReturnStmt.cpp:29` |
| - [ ] **TS-3** | 🟡 Source | High | **Integer types silently interconvert.** `i64 → u8`, `u64 → i8`, returning `-1` from a `u8` fn — all type-check with no truncation/overflow/range warning. Flows through every assign/call/return. | `analyzer/type_checker/stmt/ReturnStmt.cpp:32` |
| - [ ] **TS-4** | 🟡 Source | Medium | **Type identity is `toString()`-based** throughout comparisons → cross-module name collisions treated as identical; no namespace qualification. | `ReturnStmt.cpp:25`; `Type.h:145,151`; `MatchExpr.cpp:37,85` |
| - [ ] **TS-5** | 🟡 Source | Medium | **`if (x != nil)` does not narrow `x` to non-optional** in the branch — only `is Type` narrows. Contradicts the docs' null-safety promise. | `analyzer/type_checker/stmt/IfStmt.cpp:43-66` |
| - [ ] **TS-6** | 🟡 Source | Medium | **No definite-return / reachability analysis.** A fn declared `-> i64` with a missing `return` produces no "control reaches end" error. | `ReturnStmt.cpp` |
| - [ ] **TS-7** | 🟡 Source | Low | Records with empty fields match any record; `is` accepts arbitrary targets; `any` participates in `==` with anything. | `ReturnStmt.cpp:52-58`; `BinaryExpr.cpp:123-128`; `IsExpr.cpp:8` |

---

## 4. Language ergonomics (what's missing)

Everyday conveniences absent today (most are documented but unimplemented — noted where verified).

### Verified gaps
- [ ] **LANG-1** 🔴 Ranges (`0..n`, `0...n`) — documented but unparsed. (`frontend/parser`, no `DOT_DOT` consumer; `Lexer.cpp:429-434`)
- [ ] **LANG-2** 🔴 Parser reports ~1 error per declaration after the first (panic-mode flag is set but never reset). `frontend/parser/Parser.cpp:236`

### String & literal gaps
- [ ] **LANG-3** No **string interpolation** (`$"x = {x}"`) — biggest daily friction; everything is `"x = " + string(x)`.
- [ ] **LANG-4** No `char` type / single-quote char literals (`'a'` is a lex error).
- [ ] **LANG-5** No `\u{...}` / `\uXXXX` Unicode escapes (explicitly unsupported).
- [ ] **LANG-6** No raw/byte strings (`r"..."`, `b"..."`); no float exponents (`1e10`); no numeric suffixes (`42u8`); no octal literals.

### Pattern matching / data
- [ ] **LANG-7** `match` binds only **one** payload variable per variant; no multi-payload destructuring, no literal patterns (`case 5:`), no guards (`case X if c:`), no `or`-patterns, no nested patterns.
- [ ] **LANG-8** No **generic enums** (`enum Result<T,E>`) — only `data` and `func` are generic.
- [ ] **LANG-9** No generic bounds (`<T: Trait>`).

### Structuring / calls
- [ ] **LANG-10** No **tuples** / tuple types / multi-return; no destructuring (assign, pattern, or `for (k, v in map)`).
- [ ] **LANG-11** No **default arguments**; no **named arguments**.
- [ ] **LANG-12** No **type aliases** (`type UserId = i64`).
- [ ] **LANG-13** No **operator overloading** (`==`/`<` for user types).
- [ ] **LANG-14** No `protected` access level (only `public`/`private`).

### Operator / syntax nits
- [ ] **LANG-15** No compound bitwise/modulus assignment (`&= |= ^= %= <<= >>=`).
- [ ] **LANG-16** `list<list<i64>>` fails — lexer forms a single `>>`; needs a space. (No `>>`-splitting in the type parser.)
- [ ] **LANG-17** Lambda expression-statements rejected — `func(){...}();` (IIFE) needs wrapping parens.
- [ ] **LANG-18** Trailing commas inconsistent (allowed in list/record literals, rejected in call args/params/enum variants/generic args).

---

## 5. Runtime / codegen

| ID | Status | Sev | Issue | Location |
|---|---|---|---|---|
| - [ ] **RT-1** | 🟡 Source | High | **Exceptions don't cross FFI.** A throw from a C callback longjmps across C stack frames (UB if they hold resources); no translation to/from C errors. | `LLVMBackend.cpp:688-729` |
| - [ ] **RT-2** | 🟡 Source | High | **Debug info half-built.** DWARF line info exists (stepping works), but no variable debug-info (`llvm.dbg.declare`) → can't inspect locals in gdb/lldb; no DAP adapter. | `LLVMBackend.cpp:39-53` |
| - [ ] **RT-3** | 🟡 Source | Medium | **No tail-call optimization** — natural recursive style blows the stack (no `tail`/`musttail`/`fastcc` emitted). | `ExprCodegen.cpp` |
| - [ ] **RT-4** | 🟡 Source | Medium | **No SIMD / vector types** — numeric kernels can't use hardware. | — |
| - [ ] **RT-5** | 🟡 Source | Medium | No integer-overflow checks in arithmetic fast paths (no `nsw`/`nuw`, no trapping). | `ExprCodegen.cpp:155` |
| - [ ] **RT-6** | 🟡 Source | Low | No explicit PIE/PIC control (relocation model `std::nullopt`); link step has no `-fPIE`/`-pie`. | `LLVMBackend.cpp:35,132`; `BuildSystem.cpp:591` |

---

## 6. Stdlib / modules

**What's solid:** mature library choices throughout (libcurl, sqlite3, nlohmann/json, libarchive, libwebsockets, mosquitto). JSON, sqlite, fs, math, net (POSIX sockets) are real implementations, not stubs.

| ID | Status | Sev | Issue | Location |
|---|---|---|---|---|
| - [ ] **LIB-1** | 🟡 Source | High | **No TLS/SSL anywhere.** http relies on libcurl defaults; amqp/mqtt/websocket parse secure schemes but never configure TLS. No cert pinning, no mTLS. | `modules/net/*` |
| - [ ] **LIB-2** | 🟡 Source | High | **JWT is insecure.** `verify` doesn't check the `alg` header (alg-confusion); `base64url_decode` writes into a fixed 64-byte stack buffer → stack overflow on crafted input. | `modules/crypto/jwt.c` |
| - [ ] **LIB-3** | 🟡 Source | High | **Only one real collection (`list`) + `record` (string-keyed map).** `collections.an` is an O(n²) LINQ layer (bubble `SortBy`, linear `Distinct`/`Contains`) — no Map/Set/Queue/Stack/Tree. | `modules/collections/collections.an` |
| - [ ] **LIB-4** | 🟡 Source | Medium | No **async I/O / event loop / futures / channels** — only raw pthreads (and those race the GC). Servers fake async with threads. | — |
| - [ ] **LIB-5** | 🟡 Source | Medium | **IPv4-only** sockets (`AF_INET` hardcoded); no IPv6. | `modules/net/net.c` |
| - [ ] **LIB-6** | 🟡 Source | Medium | **No HTTP server** (websocket server is a no-op-`close` PoC). | `modules/net/websocket.c` |
| - [ ] **LIB-7** | 🟡 Source | Medium | Module C-API sharp edges: `incref`/`decref` documented as refcounting but actually pin/unpin one bit; `throw_error` not `noreturn` (modules must `return ang_nil()` after); native calls don't auto-validate arg types. | `angc/includes/Angara.h:155-156,166`; `ModuleAPI.cpp` |
| - [ ] **LIB-8** | 🟡 Source | Medium | DB drivers: sqlite only (+ its finalizer never runs due to BUG-6). No Postgres/MySQL/Redis, no connection pooling, no explicit transaction API. | `modules/data/sqlite.c` |
| - [ ] **LIB-9** | 🟡 Source | Medium | Module quality bugs: `net/rpc.c` server leaks accepted fds; `net/websocket.c::close` is a no-op; `system/process.c::run` drops the computed `exit_code`; `data/sort.c` is O(n²) insertion sort; `text/encoding.c::base32_encode` buggy shift logic; `system/os.c::run` is a shell-injection sink. | respective files |
| - [ ] **LIB-10** | 🟡 Source | Low | No logging module; no string formatting (`sprintf`); no real CLI/arg parser (subcommands/help); no YAML/protobuf/msgpack; no big integers; `adv_string` is byte-wise (corrupts multibyte UTF-8). | — |
| - [ ] **LIB-11** | 🟡 Source | Low | No file watching (inotify/kqueue); no zstd/bzip2/xz exposure; date/time is UTC-only (no timezone DB). | — |
| - [ ] **LIB-12** | 🟡 Source | Low | `testing/assert.c` is minimal (throw-on-fail only, no runner/fixtures). | `modules/testing/assert.c` |

---

## 7. Toolchain / DX

**What's solid:** diagnostics (codes, spans, carets, "did you mean", JSON), the build system as a make-replacement with first-class C/C++ FFI, the LSP (hover/def/completion/signature/diagnostics running the real pipeline), the tree-sitter grammar, and project templates.

| ID | Status | Sev | Issue | Location |
|---|---|---|---|---|
| - [ ] **TOOL-1** | 🟡 Source | High | **No package manager / registry / versioning / lockfile.** `dependencies = [...]` in a `.abs` is just `-l` flags; compose libraries only by vendoring or same-workspace projects. Existential gap for real apps. | `BuildSystem.cpp:556-566`; `CompilerDriver.cpp:248-352` |
| - [ ] **TOOL-2** | 🟡 Source | Medium | **No incremental or parallel compilation** — every build recompiles every module from scratch, serially. | `CompilerDriver.cpp:178-181` |
| - [ ] **TOOL-3** | 🟡 Source | Medium | **Docs out of sync with implementation** — variadic syntax, `foreign "header.h"`, `char`, ranges, `typeof`, operator-precedence table. | `docs/*` |
| - [ ] **TOOL-4** | 🟡 Source | Medium | **Five inconsistent version strings** — compiler `5.1.0` / backend `4.1.0` / spec `v3.1.2` / LSP `3.1.0` / README `3.0.0`. None from a single source. | `main.cpp`; `README.md` |
| - [ ] **TOOL-5** | 🟡 Source | Low | No profiler; no doc generator; no standalone linter beyond `-Wall`; formatter doesn't print lambda bodies and collapses `match` to one line. | `main.cpp`; `Formatter.cpp:439,406-414` |
| - [ ] **TOOL-6** | 🟡 Source | Low | `/opt/angara` hardcoded in ~6 places; no `ANGARA_HOME` env var (non-root / per-user installs second-class). | `BuildSystem.h:50`; `main.cpp:238,297,377,445` |

---

## Recommended fix order (highest leverage first)

1. **BUG-1** — Decide the ownership model: implement copy-on-assign *or* correct the README. *(Verified.)*
2. **GC-1 / BUG-2** — Make the GC thread-safe (STW flag + safepoints + atomics) *or* document Angara as single-threaded and gate `spawn`. *(Verified crash.)*
3. **BUG-6** — Route every allocation through `__ang_gc_alloc` so finalizers run and leaks stop (pattern exists at `ModuleAPI.cpp:295-328`).
4. **BUG-3, BUG-4, BUG-5** — Fix the OOB writes and the try/catch frame leak.
5. **TS-1** — Add trait objects / dynamic dispatch (biggest type-system gap for idiomatic code).
6. **LANG-3, LANG-1** — String interpolation + ranges (two highest-frequency daily gaps).
7. **TOOL-1** — A package manager / module resolver.
8. **LIB-2** — Fix the JWT security bugs before anyone uses `crypto.jwt`.
9. **TS-2, TS-3** — Sound generics + integer-conversion diagnostics.

---

*To regenerate or extend this audit: the underlying analysis came from six parallel deep-dive passes (GC, type system, codegen/runtime, modules/stdlib, toolchain, frontend) against `build/angc` (LLVM 22.1.7). Empirically verified items were reproduced by compiling and running small programs.*
