# SIMD Implementation Plan for Angara

> Based on `SIMD.md` analysis — this is the step-by-step, code-grounded
> implementation plan.

## Summary

The core blocker for SIMD/auto-vectorization in Angara is that **every value
is boxed** — `{i32 tag, i64 payload}` (16 bytes). Lists store boxed elements,
and access goes through a runtime function call (`__ang_list_get`). LLVM's
auto-vectorizer (O2, already running) cannot see through this.

The fix is **unboxed arrays**: contiguous raw-value storage with pointer-arithmetic
access. This unblocks auto-vectorization for free and lays the foundation for
value-type generics, fat-pointer perf, and numeric kernels.

---

## Plan overview (dependency order)

```
Phase 1  Unboxed arrays (f64[], i64[])            ← the big one
Phase 2  @inline annotation                        ← small enabler
Phase 3  Auto-vectorization verification            ← free (already works)
Phase 4  SIMD intrinsics via FFI (optional)         ← nice-to-have
Phase 5  Language-level vector types (optional)     ← largest, deferred
```

Each phase is independently committable. Phases 1-3 together deliver usable
SIMD performance on numeric kernels.

---

## Phase 1: Unboxed Arrays

### 1.0 Design decisions (to lock before coding)

#### 1.0a Syntax

Choose one:

**Option A: C-style bracket syntax** (recommended)
```
let buf as f64[];          // dynamic unboxed array
let buf as i64[];          // dynamic unboxed array
buf[i]                      // GEP + load (no call)
buf[i] = value;            // GEP + store
len(buf)                   // works like list
```

Pro: Familiar, concise, natural for FFI (`double*`).
Con: `[]` currently means fixed-size only in `foreign data` context; we'd
overload it.

**Option B: `array<T>` syntax**
```
let buf as array<f64>;
buf[i]
```

Pro: Explicit, clear distinction from `list<T>`.
Con: Extra keyword or builtin generic type.

**Recommendation: Option A** (`f64[]`) — it's the natural syntax for
contiguous memory, mirrors C/Java/Go, and reuses the existing `FIXED_ARRAY`
type family (just with dynamic size).

#### 1.0b Runtime representation

```
AngaraRawArray = {
    ObjHeader,           // {i32 type, i32 meta, ptr next}
    i64 count,           // element count
    i64 capacity,        // allocated capacity
    ptr element_buffer   // → raw T[] (NOT AngaraObject[])
}
```

Same shape as `AngaraList` but the element buffer is `T[]` (raw typed), not
`AngaraObject[]`. The element size is known statically from the type.

Heap object subtype: `OBJ_RAW_ARRAY = 14`.

#### 1.0c Relationship to `list<T>`

- `list<T>` stays unchanged (boxed, polymorphic).
- `T[]` is unboxed, monomorphic (no type erasure — the element type is known).
- Conversion helpers:
  - `list.to_array(): T[]` — copies elements, unboxes each one
  - `array.to_list(): list<T>` — copies elements, boxes each one
- Both `list<T>` and `T[]` support `len()`, `push()`, subscript.

#### 1.0d Generic interaction

`T[]` where `T` is a type parameter: **NOT supported in Phase 1.**

Unboxed arrays require the static element type and element size at codegen
time. Generics with type erasure can't provide that. Phase 1 supports only
concrete primitive element types: `i8`, `i16`, `i32`, `i64`, `u8`, `u16`,
`u32`, `u64`, `f32`, `f64`.

Value-type generics (TS-2 Phase 4, monomorphization) would later allow
`Pair<i64,i64>[]`, but that's a separate effort.

### 1.1 Step-by-step implementation

#### Step 1.1.1 — New type kind: `RAW_ARRAY`

**Files:** `angc/includes/Type.h`

Add a new `TypeKind::RAW_ARRAY` (or extend `FIXED_ARRAY` with a `dynamic`
flag). A cleaner approach: create a new type struct.

```cpp
// In Type.h, new type kind
enum class TypeKind {
    // ... existing ...
    RAW_ARRAY,   // Unboxed dynamic array: f64[], i64[], etc.
};

struct RawArrayType : Type {
    std::shared_ptr<Type> element_type;
    // element_size is derived from element_type at codegen time
    RawArrayType(std::shared_ptr<Type> elem)
        : Type(TypeKind::RAW_ARRAY), element_type(std::move(elem)) {}

    std::string toString() const override {
        return element_type->toString() + "[]";
    }
};
```

Also update:
- `substituteTypeArgs()` — recurse into `RAW_ARRAY` (defer to Phase 2 of generics)
- `sameType()` — structural comparison of element types
- `GenericInstanceType::substitute()` — same

#### Step 1.1.2 — Parser support

**Files:** `angc/frontend/parser/expr/primaryExpression.cpp` (type parsing),
`angc/includes/Token.h` (if needed)

When parsing a type expression, after a primitive type name, check for `[`
and `]` to form a raw array type.

The parser currently handles types in expression context — `as f64[]` in
var decls, `-> f64[]` in return types. Look at how `list<T>` is parsed
(angle brackets after `list`) and do similar for `[]` after primitive types.

Specifically:
- In the type parser (likely in `primaryExpression.cpp` or `Parser.cpp`),
  after recognizing a type name, peek for `[` `]` and construct a
  `RawArrayType`.

#### Step 1.1.3 — Type checker support

**Files:** `angc/analyzer/type_checker/TypeChecker.cpp`,
`angc/analyzer/type_checker/expr/SubscriptExpr.cpp`,
`angc/analyzer/type_checker/expr/VarDeclStmt.cpp`

- Allow `RawArrayType` in type annotations (var decl, return type, param type).
- Subscript on `RawArrayType`: resolve return type as `element_type`.
- Assignment to subscript: check RHS type matches `element_type`.
- `len()` on `RawArrayType`: returns `i64`.
- `push()` on `RawArrayType`: validate element type.
- Disallow `RawArrayType` as a generic argument (for now).

#### Step 1.1.4 — Runtime: `AngaraRawArray` + operations

**Files:** `angc/backend/llvm/RuntimeBuilder.cpp`,
`angc/includes/RuntimeBuilder.h`

##### 1.1.4a — LLVM struct type

Add to `generateTypes()`:

```cpp
m_raw_array_type = StructType::create(m_ctx, {
    m_obj_header_type,           // header
    Type::getInt64Ty(m_ctx),     // count
    Type::getInt64Ty(m_ctx),     // capacity
    PointerType::get(m_ctx, 0)   // element buffer (raw typed pointer)
}, "AngaraRawArray");
```

Add new OBJ subtype constant:

```cpp
static constexpr int OBJ_RAW_ARRAY = 14;
```

Add accessor in `RuntimeBuilder.h`:

```cpp
llvm::StructType* getRawArrayType() const { return m_raw_array_type; }
```

##### 1.1.4b — Runtime functions

Add to `generateListOps()` (or a new `generateRawArrayOps()`):

1. **`__ang_raw_array_new(element_size, initial_capacity) -> obj`**
   - Allocates `AngaraRawArray` header + element buffer.
   - `element_size` is the static sizeof(T) — passed as a constant.
   - Returns boxed `AngaraObject` (TAG_OBJ, payload = heap ptr).

2. **`__ang_raw_array_get(array_obj, index as i64) -> raw_value as i64`**
   - Bounds-check index against count.
   - Load element at `buf + index * elem_size`.
   - Return raw value as `i64` (caller converts to appropriate type).
   - If OOB, return 0 (matching list_get's nil-semantics for now; later
     we can add debug-mode traps).

3. **`__ang_raw_array_set(array_obj, index as i64, value as i64) -> void`**
   - Bounds-check + store.

4. **`__ang_raw_array_push(array_obj, value as i64) -> void`**
   - Grow if needed (realloc, same pattern as list_push).
   - Store at count, increment count.

5. **`__ang_raw_array_len(array_obj) -> i64`**
   - Load count field.

For `__ang_raw_array_get/set/push`, we need to know the element size.
Options:
- **Option A:** Store `elem_size` in the `AngaraRawArray` struct as a field.
  Pro: One runtime function handles all element types. Con: Extra field,
  runtime lookup costs nothing but adds 8 bytes.
- **Option B:** Generate per-type functions (`__ang_raw_array_i64_get`,
  `__ang_raw_array_f64_get`). Pro: No elem_size field needed. Con: Code
  bloat.
- **Option C (recommended):** Store `elem_size` in the header (field 3
  of ObjHeader or as a new field). This is cheap (one i32/i64) and lets
  all operations be generic.

Let's add `elem_size` to the struct:

```
AngaraRawArray = { ObjHeader, i64 count, i64 capacity, i32 elem_size, ptr buf }
```

Or pack `elem_size` into the existing `i32 meta` field of ObjHeader
(since GC is minimal). Actually, let's keep it clean: add it as a field.

##### 1.1.4c — Integration with freestanding mode

In `generateFreestandingStubs()`, add stubs for raw array ops if needed
(they use malloc/free internally).

#### Step 1.1.5 — Codegen: `cgSubscript` for raw arrays

**Files:** `angc/backend/llvm/expr/ExprCodegen.cpp`

In `cgSubscript()`, after the existing list/record checks, add:

```cpp
// Check if the object is a raw array type
auto type_it = m_type_checker.getExpressionTypes().find(e.object.get());
if (type_it != m_type_checker.getExpressionTypes().end() &&
    type_it->second->kind == TypeKind::RAW_ARRAY) {
    auto raw_arr_type = std::dynamic_pointer_cast<RawArrayType>(type_it->second);
    auto* obj = cg(e.object);
    auto* idx = cg(e.index);

    // Extract the raw i64 index
    auto* idx_val = getI64(idx);

    // Unbox the array pointer
    auto* payload = builder->CreateExtractValue(obj, {1});
    auto* arr_ptr = builder->CreateIntToPtr(payload,
        rt->getRawArrayType()->getPointerTo());

    // Load count for bounds check (debug mode)
    auto* count = builder->CreateLoad(i64_ty,
        builder->CreateStructGEP(rt->getRawArrayType(), arr_ptr, 1));

    // Load element buffer pointer
    auto* buf = builder->CreateLoad(ptr_ty,
        builder->CreateStructGEP(rt->getRawArrayType(), arr_ptr, 3));

    // Determine element LLVM type from RawArrayType
    llvm::Type* elem_llvm_type = llvmTypeForLocalKind(
        localKindForType(raw_arr_type->element_type));

    // GEP into the buffer (using elem_llvm_type as the pointee type)
    auto* elem_ptr = builder->CreateGEP(elem_llvm_type, buf, {idx_val});

    // Load the raw value
    auto* raw_val = builder->CreateLoad(elem_llvm_type, elem_ptr);

    // Box the result back into AngaraObject
    return boxRaw(raw_val, localKindForType(raw_arr_type->element_type));
}
```

This is the key transformation: **GEP + load replaces the `__ang_list_get`
function call.** LLVM now sees a straight-line pointer-arithmetic access
over contiguous memory — auto-vectorizable.

Note: we use the compile-time `elem_llvm_type` for the GEP, not the runtime
function. This is what makes it auto-vectorizable — LLVM knows the element
size and type statically.

#### Step 1.1.6 — Codegen: assignment to raw array subscript

**Files:** `angc/backend/llvm/expr/ExprCodegen.cpp` (in `cgAssign`)

Similar to 1.1.5 but with `CreateStore` instead of `CreateLoad`:

```cpp
// Unbox the value
auto* raw_val = unboxToRaw(v, localKindForType(raw_arr_type->element_type));
// GEP + store
builder->CreateStore(raw_val, elem_ptr);
```

#### Step 1.1.7 — Codegen: `cgList` for raw arrays (raw array literal)

**Files:** `angc/backend/llvm/expr/ExprCodegen.cpp`

Add a new `cgRawArray()` or extend `cgList()`:

For a raw array literal like `[1.0, 2.0, 3.0] as f64[]`:
1. Allocate `AngaraRawArray` with initial capacity = element count.
2. For each element, unbox and store directly into the buffer.
3. Return the boxed array.

Alternative: generate `__ang_raw_array_new` + `__ang_raw_array_push` calls.
The direct approach (pre-size + store) is more efficient and avoids
reallocations.

#### Step 1.1.8 — Codegen: `len()` support for raw arrays

**Files:** `angc/backend/llvm/expr/ExprCodegen.cpp` (in `cgCall`)

In the `len` / `length` / `size` / `count` handler, add a raw-array branch
that extracts count directly (GEP + load) instead of calling `__ang_len`.

#### Step 1.1.9 — Codegen: `push()` support for raw arrays

In the `push` / `add` handler, detect raw array type and call
`__ang_raw_array_push` (or emit inline GEP+store for maximum performance).

#### Step 1.1.10 — Global variables of raw array type

**Files:** `angc/backend/llvm/TopLevel.cpp`

In `codegenGlobalVarDecl`, if the variable type is `RawArrayType`:
- The global is still a boxed `AngaraObject` (just like lists).
- The initializer is zeroinitializer (nil) — raw arrays are heap-allocated
  at runtime, same as lists.

#### Step 1.1.11 — Reflection / `typeof` support

**Files:** `angc/backend/llvm/rt/Conversions.cpp`

Update `is` operator for raw arrays: check `OBJ_RAW_ARRAY` subtype.

#### Step 1.1.12 — Conversion: list ↔ raw array

Add two helper functions (could be in a standard library module, or as
runtime functions):

- `__ang_list_to_raw_array(list_obj, elem_size) -> raw_array_obj`
  Iterates list, unboxes each element, stores in raw array.

- `__ang_raw_array_to_list(array_obj) -> list_obj`
  Iterates raw array, boxes each element, pushes to list.

These can be implemented as Angara source functions (using `foreign func`
loops) or as C runtime functions. For simplicity, implement as runtime
functions in `Collections.cpp`.

#### Step 1.1.13 — Testing

Create test files:
- `tests/arrays/basic.an`: create, push, read, write
- `tests/arrays/bench_dot_product.an`: dot product over f64[]
- `tests/arrays/conversion.an`: list ↔ array roundtrip
- `tests/arrays/bounds.an`: out-of-bounds behavior

Verify LLVM IR: compile dot product, dump IR, confirm GEP+load pattern
(replacing __ang_list_get calls).

---

## Phase 2: `@inline` Annotation

### Goal

Let the programmer mark hot functions for inlining so per-element helpers
in a loop body don't introduce call boundaries that block vectorization.

### Implementation

#### Step 2.1 — Parser

**Files:** `angc/frontend/parser/stmt/functionStatement.cpp`

Add `@inline` as a recognized annotation on `func` declarations. The
annotation parsing infrastructure is already in place (`@on_throw` for
RT-1, `@own` for strings). Add `inline` to the annotation set.

Store as a flag on `FuncStmt`: `bool is_inline = false;`

**Files:** `angc/includes/Stmt.h` — add `bool is_inline` to `FuncStmt`.

#### Step 2.2 — Codegen

**Files:** `angc/backend/llvm/TopLevel.cpp` (in `codegenFunctionDecl`)

After creating the LLVM function, if `stmt.is_inline`:

```cpp
fn->addFnAttr(llvm::Attribute::AlwaysInline);
// or for a hint: fn->addFnAttr(llvm::Attribute::InlineHint);
```

Use `AlwaysInline` — the programmer explicitly asked for it and it's
critical for vectorization.

Also set `fn->setLinkage(llvm::Function::LinkOnceODRLinkage)` so the
function body is available across modules for inlining (if using
`InlineHint`; for `AlwaysInline`, internal linkage is fine as long as
all call sites are in the same module).

#### Step 2.3 — Testing

```angara
@inline
func square(x as f64) -> f64 {
    return x * x;
}

func sum_of_squares(buf as f64[]) -> f64 {
    let sum = 0.0;
    let i = 0;
    while (i < len(buf)) {
        sum = sum + square(buf[i]);  // square is inlined → no call in loop
        i = i + 1;
    }
    return sum;
}
```

Verify the IR: `square` should not appear as a `call` in the loop body.

---

## Phase 3: Auto-Vectorization Verification

### Goal

Confirm LLVM's loop vectorizer produces SIMD instructions for clean loops
over unboxed arrays.

### What's needed

Nothing — LLVM's `LoopVectorizePass` is already in the O2 pipeline
(`LLVMBackend.cpp:148-150`). Once Phases 1-2 produce clean pointer-arithmetic
IR, auto-vectorization happens automatically.

### Verification steps

1. Write a dot-product benchmark:
```angara
func dot_product(a as f64[], b as f64[]) -> f64 {
    let sum = 0.0;
    let i = 0;
    while (i < len(a)) {
        sum = sum + a[i] * b[i];
        i = i + 1;
    }
    return sum;
}
```

2. Compile at O2 and dump assembly:
```bash
./angc build tests/arrays/bench_dot_product.an --dump-asm
```

3. Verify NEON instructions (ARM64) or SSE/AVX (x86-64):
   - ARM64: look for `fmla v0.2d, v1.2d, v2.2d` (vector FMA)
   - x86-64: look for `mulpd`, `addpd`, `vfmadd213pd`

4. If not vectorizing, investigate:
   - Loop trip count: is it known? Try `while (i + 4 <= len(a))` for unroll
   - Aliasing: do `a` and `b` overlap? Try `restrict`-like annotation
   - Add `#pragma clang loop vectorize(enable)` via LLVM metadata

---

## Phase 4: SIMD Intrinsics via FFI (Optional)

### Goal

Enable direct calls to SSE/NEON/AVX intrinsics for manual vectorization.

### Approach

SIMD intrinsics (`_mm_add_ps`, `vaddq_f32`, etc.) are ordinary C functions
callable via `foreign func`. The gap is that they take/return 128/256/512-bit
values (`__m128`, `int32x4_t`) which the FFI marshalling doesn't handle.

### Implementation

#### Step 4.1 — 128-bit type support in FFI

**Files:** `angc/backend/llvm/expr/ExprCodegen.cpp` (in `resolveCFieldType`,
`marshalAngaraToC`, `marshalCToAngara`)

Add handling for `<N x float>` and `<N x i32>` LLVM vector types:

```cpp
// In resolveCFieldType:
if (isSimdVectorType(type))  // check for __m128, int32x4_t, etc.
    return llvm::FixedVectorType::get(llvm::Type::getFloatTy(*ctx), 4);

// In marshalAngaraToC / marshalCToAngara:
// Pass-through for vector types (bitcast from i128 AngaraObject payload)
```

#### Step 4.2 — SIMD module (wrapper)

Alternatively, add a `simd` standard library module (`modules/simd/simd.c`)
that wraps common intrinsics:

```c
// simd.c — wraps NEON/SSE intrinsics into Angara-friendly signatures
#include <arm_neon.h>  // or <xmmintrin.h>

// Takes two f64[] arrays, stores result in result[]
void simd_f64x4_add(int64_t n, double* a, double* b, double* result) {
    for (int64_t i = 0; i + 3 < n; i += 4) {
        float64x2_t va = vld1q_f64(a + i);
        float64x2_t vb = vld1q_f64(b + i);
        float64x2_t vr = vaddq_f64(va, vb);
        vst1q_f64(result + i, vr);
    }
}
```

Declare in Angara:
```angara
foreign func simd_f64x4_add(n as i64, a as f64[], b as f64[], result as f64[]);
```

Since Phase 1 unboxed arrays are directly passable as `double*` to C, this
works without any FFI changes.

**Recommendation:** the wrapper module approach (4.2) is simpler and more
Angara-idiomatic. Do 4.1 only if benchmarks show the wrapper overhead is
unacceptable.

---

## Phase 5: Language-Level Vector Types (Optional, Deferred)

### Goal

First-class `vec4<f32>`, `v4i32` types with operator overloading for
arithmetic.

### Scope

This is a large type-system change:
- New `TypeKind::VECTOR`
- Parser: vector literals (`vec4(1.0, 2.0, 3.0, 4.0)`)
- Type checker: element-wise arithmetic, swizzles
- Codegen: map to LLVM `<4 x float>`, emit `fadd`, `fmul` on vector types
- Integration with generics and trait system

**Not needed for Phase 1-4.** The auto-vectorizer handles most real-world
cases. Only implement if:
1. Phases 1-3 are proven and shipped
2. A real workload (e.g., game physics, shader compilation) shows the
   auto-vectorizer isn't sufficient
3. Manual vectorization with intrinsics (Phase 4) is too verbose

---

## Cross-Cutting Concerns

### Memory management

`AngaraRawArray` uses the same `ObjHeader` as all heap objects. The allocator
(`__ang_gc_alloc`) already handles arbitrary sizes. The `free` path needs to:
1. Free the element buffer (`free(buf)`)
2. Free the header

This should be integrated into the existing finalization path (or handled
directly since there's no GC — it's `malloc`/`free`).

### Debug bounds checking

For debug builds, emit bounds-check branches in `cgSubscript` for raw arrays:

```cpp
// Debug mode bounds check
if (m_debug) {
    auto* in_bounds = builder->CreateAnd(
        builder->CreateICmpSGE(idx_val, zero),
        builder->CreateICmpSLT(idx_val, count));
    auto* trap = llvm::Intrinsic::getOrInsertDeclaration(
        mod.get(), llvm::Intrinsic::ubsantrap,
        {llvm::Type::getInt8Ty(*ctx)});
    // ...conditional trap on OOB
}
```

### Interaction with Chaperone (v5 ownership)

Raw arrays are heap-allocated objects, same as lists. They should participate
in the ownership tracking (`m_tracked_types`) if needed. For Phase 1, they
can be treated like lists (reference semantics, no ownership tracking unless
the element type requires it).

Since element types are primitives in Phase 1 (no owned/complex types), no
ownership tracking is needed for elements. The array itself is a reference
type — assignment copies the reference, not the data.

### Progress tracking

| Phase | Step | Status |
|---|---|---|
| 1.1.1 | New type kind RAW_ARRAY | pending |
| 1.1.2 | Parser support | pending |
| 1.1.3 | Type checker support | pending |
| 1.1.4 | Runtime (AngaraRawArray + ops) | pending |
| 1.1.5 | cgSubscript for raw arrays | pending |
| 1.1.6 | Assignment to raw array subscript | pending |
| 1.1.7 | Raw array literals | pending |
| 1.1.8 | len() support | pending |
| 1.1.9 | push() support | pending |
| 1.1.10 | Global variables | pending |
| 1.1.11 | is/typeof support | pending |
| 1.1.12 | list ↔ array conversion | pending |
| 1.1.13 | Tests | pending |
| 2.1 | @inline parser | pending |
| 2.2 | @inline codegen | pending |
| 2.3 | @inline tests | pending |
| 3 | Auto-vectorization verification | pending (depends on 1+2) |
| 4 | SIMD intrinsics | deferred |
| 5 | Vector types | deferred |

---

## Risks and Mitigations

| Risk | Severity | Mitigation |
|---|---|---|
| `f64[]` syntax is ambiguous with `foreign data` fixed arrays | Low | Fixed arrays only appear in `foreign data` context; raw arrays use `[]` without size |
| Raw arrays don't work with generics | Medium | Documented limitation; value-type generics (TS-2.4) will address this |
| Auto-vectorizer doesn't trigger on real code | Medium | Phase 3 verification catches this; LLVM metadata pragmas as fallback |
| Code size bloat from per-type codegen | Low | LLVM's identical-code folding handles duplicate GEP sequences; the runtime functions are generic (elem_size field) |
| Interaction with Chaperone ownership | Low | Raw arrays are reference types; same semantics as lists for now |
