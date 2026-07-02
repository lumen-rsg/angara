# Angara SIMD — Analysis and Roadmap

> RT-4: "No SIMD / vector types — numeric kernels can't use hardware."
>
> This document analyzes what blocks SIMD performance in Angara today,
> why explicit vector types aren't the right first step, and what the
> minimal unblocking path is.

## Executive summary

**SIMD is not blocked by a missing "vector types" language feature.** It's blocked by the same foundational issue that limits all numeric performance: **every value is boxed**. An `i64` in a list is stored as `{i32 tag, i64 payload}` (12–16 bytes), accessed via a runtime function call (`__ang_list_get`), not as a contiguous raw array. LLVM's auto-vectorizer (already running at O2) can't vectorize loops over boxed/tagged data — it needs contiguous, uniformly-typed memory and straight-line element access.

The unblocking path is:

1. **Unboxed arrays** (`f64[]`, `i64[]` — contiguous raw-value storage).
2. **`@inline` on functions** (so hot loops over arrays aren't broken by call boundaries).
3. **LLVM auto-vectorization** then works for free on clean loops.
4. **Explicit SIMD intrinsics** (SSE/NEON via FFI) as a nice-to-have on top.

Language-level vector types (`vec4<f32>`, `v4i32`) are a *separate, larger* effort and not the bottleneck — they add ergonomics for manual vectorization, but the auto-vectorizer already handles clean loops if the data layout is right.

---

## 1. Current state — why LLVM can't vectorize Angara code

### 1.1 Boxed value representation

Every Angara value is an `AngaraObject`:

```
AngaraObject = { i32 tag, i64 payload }    // 12 bytes (16 with alignment padding)
```

(`RuntimeBuilder.cpp:51-55`)

- Scalars: `tag = TAG_I64`, `payload = 42`.
- Floats: `tag = TAG_F64`, `payload = bitcast(3.14)`.
- Objects (strings, lists, records): `tag = TAG_OBJ`, `payload = heap pointer`.

Every variable, parameter, return value, list element, and record field is one of these. There is no "raw `i64`" storage for `list<i64>` — each element carries its own tag.

### 1.2 List storage is boxed

A `list<i64>` at runtime is:

```
AngaraList = { ObjHeader, i64 count, i64 capacity, ptr→ AngaraObject[] }
```

(`RuntimeBuilder.cpp:87-92`)

The element buffer is `AngaraObject[]` — an array of `{tag, payload}` pairs. Even for `list<i64>`, every element is a 16-byte boxed value, not a raw 8-byte `i64`.

### 1.3 List access is a function call

Reading `items[i]` lowers to:

```llvm
%elem = call AngaraObject @__ang_list_get(AngaraObject %list, AngaraObject %idx)
```

(`ExprCodegen.cpp` → `Collections.cpp:166-207`)

This is an indirect function call that: (a) bounds-checks, (b) GEPs into the element buffer, (c) loads a 16-byte `{tag, payload}` struct, (d) returns it by value. The function call boundary and the tag-dispatch prevent LLVM from seeing the underlying contiguous memory.

### 1.4 What a typical numeric loop looks like in IR

```angara
let sum = 0;
let i = 0;
while (i < len(items)) {
    sum = sum + items[i];
    i = i + 1;
}
```

Lowers to (simplified):

```llvm
loop:
  %i = phi i64 [0, entry], [%i.next, body]
  %sum = phi AngaraObject [%sum0, entry], [%sum1, body]
  %len = call i64 @__ang_len(AngaraObject %items)
  %cond = icmp slt i64 %i, %len
  br i1 %cond, %body, %done
body:
  %idx = insertvalue AngaraObject {i32 2, i64 undef}, i64 %i, 1
  %elem = call AngaraObject @__ang_list_get(AngaraObject %items, AngaraObject %idx)
  %elem_val = extractvalue AngaraObject %elem, 1
  %sum_val = extractvalue AngaraObject %sum, 1
  %new_val = add nsw i64 %sum_val, %elem_val
  %sum1 = insertvalue AngaraObject {i32 2, i64 undef}, i64 %new_val, 1
  %i.next = add nsw i64 %i, 1
  br label %loop
```

LLVM sees: a function call per iteration (`__ang_list_get`), tagged-union boxing/unboxing, and an opaque `__ang_len` call. **It cannot prove the element buffer is contiguous `i64[]`**, so it cannot auto-vectorize.

### 1.5 What would make it vectorizable

The same loop over an unboxed `i64[]`:

```llvm
loop:
  %i = phi i64 [0, entry], [%i.next, body]
  %sum = phi i64 [0, entry], [%sum1, body]
  %ptr = getelementptr i64, ptr %buf, i64 %i
  %val = load i64, ptr %ptr
  %sum1 = add nsw i64 %sum, %val
  %i.next = add nsw i64 %i, 1
  %cond = icmp slt i64 %i.next, %n
  br i1 %cond, %body, %done
```

This is a textbook auto-vectorization candidate. LLVM at O2 transforms it into NEON/SSE vector operations (`<4 x i64>` adds, unrolled) **for free** — no language change needed.

---

## 2. The unblocking path (in dependency order)

### Phase 1: Unboxed arrays

**Goal:** a contiguous raw-value array type where elements are stored inline (not boxed), enabling pointer-arithmetic loops.

**Syntax options (not yet decided):**
- `let buf as f64[]` — C-style array type (natural fit for FFI/kernel code).
- `let buf as array<f64>` — explicit, distinct from `list<T>`.
- `let buf as raw list<f64>` — modifier on the existing list.

**Implementation:**
- New `TypeKind::FIXED_ARRAY` already exists (`Type.h:33`) but is only used for `foreign data` C arrays (`i8[256]`). Extend it to support user-facing dynamically-sized arrays.
- Runtime: a new `AngaraRawArray` type `{header, count, capacity, ptr→ raw elements}` — same structure as `AngaraList` but the element buffer is raw typed (no tags). Allocation knows the element size from the static type.
- Codegen: `buf[i]` on a `f64[]` lowers to `getelementptr` + `load double` — no function call, no tag dispatch. LLVM sees straight-line pointer arithmetic.
- Bound checking: optional, can be a debug-mode insertion (`llvm.ubsantrap` or a simple `icmp` + branch).

**What this unblocks:**
- Auto-vectorization of numeric loops (free, at O2).
- FFI buffers — a `f64[]` is directly passable to C functions expecting `double*` (no marshalling).
- The foundation for value-type generics (TS-2 Phase 4).

**Estimated scope:** medium-large. New type kind + runtime object + subscript codegen + list→array conversion helpers. Does NOT require changing the existing `list<T>` (both coexist).

### Phase 2: `@inline` annotation

**Goal:** let the programmer mark hot functions for inlining, so per-element helpers in a loop body are eliminated.

**Why it matters:** even with unboxed arrays, a loop that calls a non-inlined function per element (e.g., `buf[i] = square(buf[i])`) has a call boundary that blocks vectorization. LLVM inlines `internal` functions automatically, but exported or cross-module functions can't be inlined.

**Implementation:**
- Parse `@inline` on `func` declarations (same `@annotation` path as `@on_throw`).
- Emit the function with `AlwaysInline` LLVM attribute (or `InlineHint`).
- Small change — ~15 lines of parser + 2 lines of codegen attribute setting.

**Estimated scope:** small.

### Phase 3: Auto-vectorization (free)

**Goal:** LLVM's loop vectorizer produces NEON/SSE instructions for clean loops over unboxed arrays.

**What's needed:** nothing — LLVM at O2 already has the loop vectorizer enabled. Once the IR is clean pointer arithmetic over contiguous memory (Phase 1), it works automatically.

**Verification:** compile a dot-product loop over `f64[]` at O2, dump the assembly, confirm `fadd` → `fmla v0.4s, v1.4s, v2.4s` (NEON) or equivalent SSE.

**Estimated scope:** zero (it's already there).

### Phase 4: Explicit SIMD intrinsics (optional, nice-to-have)

**Goal:** let programmers call hardware-specific intrinsics directly for manual vectorization.

**Approach:** SSE/NEON intrinsics (`_mm_add_ps`, `vaddq_f32`) are ordinary C functions callable via `foreign func`. The only gap is that they take/return 128-bit values (`__m128`, `int32x4_t`) which the FFI marshalling doesn't handle today (it marshals `i32`/`i64`/`ptr`/`void`).

**Implementation:**
- Extend `resolveCFieldType` / `marshalAngaraToC` to handle 128-bit types (represented as `i128` or `<4 x float>` in LLVM IR).
- Or: add a small `simd` module (C extension) that wraps the common intrinsics into Angara-friendly functions taking/returning `f64[]` (which Phase 1 unboxed arrays provide).

**Estimated scope:** medium (FFI marshalling extension) or small (wrapper module).

### Phase 5: Language-level vector types (optional, largest)

**Goal:** first-class `vec4<f32>`, `v4i32` types with their own arithmetic operators.

**Why it's last:** it's the most ergonomic for manual vectorization (Rust/Swift-style), but it's also the largest type-system change (new `TypeKind`, parser support for vector literals, operator overloading for `+ - * /` on vectors, codegen for vector ops). The auto-vectorizer (Phase 3) handles most real-world cases without it.

**Estimated scope:** large. New type kind + arithmetic codegen + syntax. Only worth doing after Phases 1-3 are proven and a real workload shows the auto-vectorizer isn't enough.

---

## 3. Relationship to other deferred work

The unboxed-array work (Phase 1) is the **same foundational change** that several other deferred audit items depend on:

| Deferred item | Why it needs unboxed storage |
|---|---|
| **TS-2 Phase 4** (value-type generics, monomorphization) | `data Pair<i64,i64>` should be an inline `{i64, i64}` struct, not a heap record of boxed fields. Same inline-storage mechanism. |
| **TS-1 fat-pointer perf** | Trait objects currently box (one allocation per upcast). A fat pointer would carry the vtable inline — same "widen the value representation" change. |
| **RT-5 overflow performance** | NSW/NUW flags (already shipped) help, but full numeric perf needs unboxed storage so the flags apply to real contiguous data. |
| **LIB-4** (numeric kernels / compute) | Matrix multiply, FFT, image processing all need contiguous unboxed arrays. |

**Recommendation:** unboxed arrays should be the next foundational performance work, unblocking SIMD + value-type generics + numeric kernels in one change. It's the single highest-leverage codegen investment remaining.

---

## 4. Quick reference: relevant code sites

| Concern | Location |
|---|---|
| Boxed value representation | `RuntimeBuilder.cpp:51-55` (`AngaraObject = {i32, i64}`) |
| List runtime (boxed elements) | `RuntimeBuilder.cpp:87-92` (`AngaraList`); `rt/Collections.cpp:166-207` (`list_get`) |
| List subscript codegen | `ExprCodegen.cpp` `cgSubscript` — `__ang_list_get` |
| Existing FIXED_ARRAY type | `Type.h:33` (`TypeKind::FIXED_ARRAY`), only for foreign `i8[256]` |
| Foreign data (inline C struct) | `TopLevel.cpp:433+` (`codegenForeignDataDecl`) — the precedent for inline-layout types |
| Raw-signature optimization | `TopLevel.cpp:79-113` — unboxed function params (the precedent for raw-value passing) |
| Optimization level | `LLVMBackend.cpp:148-150` — O2 (non-debug), O0 (debug) |
| Auto-vectorizer | LLVM `LoopVectorizePass` — part of the O2 default pipeline, already running |
| Arithmetic flags (NSW/NUW) | `ExprCodegen.cpp` `cgBinary` — already shipped (RT-5) |

---

## 5. Suggested staging for implementation

1. **Design doc:** decide the syntax (`f64[]` vs `array<f64>` vs other), the runtime representation, and the relationship to `list<T>` (conversion, interop).
2. **Phase 1 (unboxed arrays):** new type kind, runtime `AngaraRawArray`, subscript codegen, `len()` support, list→array conversion. Test with a dot-product benchmark.
3. **Phase 2 (`@inline`):** parse + attribute. Test that a loop calling an inlined helper vectorizes.
4. **Phase 3 (verify auto-vectorization):** benchmark + assembly dump.
5. **Phase 4+ (intrinsics, vector types):** only if benchmarks show the auto-vectorizer isn't sufficient.

Each phase is independently committable. Phases 1-3 together deliver usable SIMD performance.
