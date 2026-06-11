# GC Implementation Tracker

## Instructions
- Commit after every completed stage
- Do NOT mention Claude or co-author in commit messages
- Keep this file updated with progress notes

## Architecture: GC Interface

The GC is abstracted behind a generic interface so multiple collector strategies (ARC, MarkSweep, Generational, etc.) can coexist. The interface is defined as LLVM IR generation methods — since the entire runtime is emitted as LLVM IR, the "interface" is a C++ abstract class whose implementations generate different LLVM IR for the same logical operations.

### GC Interface Methods
Every GC implementation must provide:
- `generateTypes()` — Generate GC-specific struct types (header layout, metadata)
- `generateGlobals()` — Generate GC-specific global variables
- `generateFunctions()` — Generate GC-specific runtime functions
- `getHeaderType()` — Return the ObjHeader struct type for this GC
- `emitAlloc(irbuilder, size, obj_type)` — Emit IR for an allocation
- `emitStoreTracking(irbuilder, value)` — Emit IR for write/store tracking (e.g., clear_unique, write barrier)
- `emitRootFrameEntry(irbuilder, frame_ptr)` — Emit IR at function entry
- `emitRootFrameExit(irbuilder)` — Emit IR at function exit
- `emitThreadRegister(irbuilder, state_ptr)` — Emit IR for thread registration
- `emitThreadUnregister(irbuilder, state_ptr)` — Emit IR for thread cleanup
- `getAPIVtableSlots()` — Return the vtable function pointers for native module API
- `generateFreestandingStubs()` — Generate no-op stubs for bare-metal

### Implementations
- `MarkSweepGC` — Precise mark-and-sweep with tri-color marking, per-function root frames, uniqueness bit
- (Future) `GenerationalGC` — Generational mark-and-sweep with nursery
- (Legacy) `RefCountGC` — The original ARC (for backward compat or freestanding)

## Progress

### Stage 1: Infrastructure ✅
- [x] Create GC interface class
- [x] Modify ObjHeader layout
- [x] Add GC globals
- [x] Generate `__ang_gc_alloc`
- [x] Update header initialization sites
- [x] Verify: compiles and runs identically

### Stage 2: Remove ARC from codegen ✅
- [x] Remove decref from cgAssign
- [x] Remove decref from main cleanup
- [x] Remove incref from makeStr
- [x] Verify: compiles, runs, leaks memory

### Stage 3: Remove ARC from runtime ✅
- [x] Remove incref/decref from Collections, Strings, ControlFlow, IO, Conversions
- [x] Replace malloc with gc_alloc in constructors
- [x] Add env_count to closure layout
- [x] Add clear_unique at container stores
- [x] Verify: compiles, runs, no manual memory management

### Stage 4: Implement the collector ✅
- [x] Implement __ang_gc_scan (type-dispatched traversal)
- [x] Implement __ang_gc_mark (tri-color marking)
- [x] Implement __ang_gc_mark_roots (thread list → root frames → mark)
- [x] Implement __ang_gc_sweep (walk allocation list)
- [x] Implement __ang_gc_collect (mark_roots + sweep)
- [x] Wire threshold check into gc_alloc
- [x] Verify: GC compiles and IR verifies; all LLVM tests pass

### Stage 5: Root frames + thread registration
- [x] Implement push_frame / pop_frame
- [x] Implement thread_register / thread_unregister
- [x] Implement safepoint (stop-the-world)
- [x] Add root frame setup to codegen
- [x] Add thread registration to main + thread trampoline
- [x] Verify: multi-threaded GC with precise root scanning

### Stage 6: Module API + freestanding
- [ ] Update AngaraAPI vtable (gc_pin / gc_unpin)
- [ ] Update freestanding stubs
- [ ] Verify: native modules and bare-metal work

### Stage 7: String concat optimization
- [ ] Wire is_unique check into string_concat
- [ ] Add clear_unique at aliasing stores
- [ ] Reset is_unique in sweep
- [ ] Verify: in-place string concat works under GC

## Notes
- ObjHeader grows from 12 → 16 bytes
- Closure gains env_count field
- All incref/decref calls removed from entire codebase

## Benchmark: Angara (GC) vs C (Stage 5)

Results after Stage 5 (mark-sweep with root frames), --release / -O2, 5-run averages:

| Benchmark | Angara | C | Ratio |
|---|---|---|---|
| Integer Loop (500M iter) | 0.842s | 0.098s | 8.59x |
| String Building (100K) | 0.700s | 0.084s | 8.33x |
| Recursive Fibonacci (n=40) | 0.824s | 0.352s | 2.34x |
| Bubble Sort (20K) | 1.084s | 0.560s | 1.93x |
| Data Class Churn (500K) | 0.130s | 0.080s | 1.62x |
| Matrix Multiply (200x200) | 0.100s | 0.078s | 1.28x |
| Prime Sieve (1M) | 0.096s | 0.088s | 1.09x |
| List Ops (1M items) | 0.086s | 0.078s | 1.10x |

### Analysis

- **Under 1.3x (near C speed):** Prime sieve, lists, matrix multiply — these allocate few objects relative to compute, so GC overhead is negligible.
- **1.6–2.3x (moderate):** Bubble sort, dataclass churn, recursive fibonacci — allocation-heavy but objects live long enough to amortize collection cost.
- **~8x (heavy GC pressure):** Integer loop and string building — the GC threshold of 1024 triggers a full mark+sweep every 1024 allocations. Integer loop creates 500M boxed integers → ~500K collections. String building creates many short-lived concatenation results.

### Root causes of overhead

1. **Low collection threshold (1024)** — far too aggressive for allocation-heavy workloads. Each collection walks the entire allocation list, marking every live object.
2. **No uniqueness reuse** — string concat always allocates a new string instead of reusing the buffer when the source is unique (Stage 7 will fix this).
3. **No generational optimization** — every collection scans all live objects regardless of age. A future generational collector would only scan recently-allocated objects.

### Improvement roadmap

- **Raise threshold to 64K–128K** — simplest fix, expected to bring the 8x cases down to 2–3x by reducing collection frequency 60–120x.
- **Stage 7 (is_unique)** — enables in-place string concat, eliminates most string allocation overhead.
- **Future: generational GC** — nursery-based collection would make short-lived objects nearly free.
