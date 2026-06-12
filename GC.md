# GC Implementation Tracker

## Instructions
- Commit after every completed stage
- Do NOT mention Claude or co-author in commit messages
- Keep this file updated with progress notes

## Architecture: GC Interface

The GC is abstracted behind a generic interface so multiple collector strategies (MarkSweep, Chaperone, Generational, etc.) can coexist. The interface is defined as LLVM IR generation methods — since the entire runtime is emitted as LLVM IR, the "interface" is a C++ abstract class whose implementations generate different LLVM IR for the same logical operations.

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
- `ChaperoneGC` — Protein-folding-inspired GC: bump-arena allocation, mark-sweep collection, simulated-annealing compaction, arena recycling
- (Future) `GenerationalGC` — Generational mark-and-sweep with nursery

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
- [x] Update AngaraAPI vtable (gc_pin / gc_unpin)
- [ ] Update freestanding stubs
- [ ] Verify: native modules and bare-metal work

### Stage 7: String concat optimization
- [x] Wire is_unique check into string_concat
- [x] Add clear_unique at aliasing stores
- [x] Reset is_unique in sweep
- [x] Verify: in-place string concat works under GC

### Performance fixes
- [x] Reset gc_count after collection (was never reset → every alloc triggered GC)
- [x] Raise threshold from 1024 → 65536

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

## Benchmark: Angara (GC) vs C (Stage 6–7 fixes)

After gc_count reset, threshold raise (65536), string concat is_unique fix, and gc_pin/gc_unpin:

| Benchmark | Angara | C | Ratio | Change |
|---|---|---|---|---|
| Integer Loop (500M iter) | 0.792s | 0.088s | 9.00x | — |
| String Building (100K) | 0.090s | 0.104s | **0.86x** | **9.7x faster** |
| Recursive Fibonacci (n=40) | 0.786s | 0.364s | 2.15x | slightly faster |
| Bubble Sort (20K) | 1.132s | 0.590s | 1.91x | — |
| Data Class Churn (500K) | 0.132s | 0.072s | 1.83x | — |
| Matrix Multiply (200x200) | 0.118s | 0.090s | 1.31x | — |
| Prime Sieve (1M) | 0.128s | 0.086s | 1.48x | — |
| List Ops (1M items) | 0.080s | 0.072s | 1.11x | — |

### Analysis

- **String building: FASTER than C (0.86x).** The is_unique optimization enables in-place string concatenation — when a string has no other references, concat reuses its buffer instead of allocating. This eliminates most string allocation overhead entirely.
- **Integer loop (9x):** Still slow because it creates 500M boxed integers. The bottleneck is allocation overhead (gc_alloc linked-list prepend + tagged union boxing), not GC collection frequency. A generational collector with bump allocation would help here.
- **Under 2x:** Most benchmarks remain in the 1.1–1.9x range, which is typical for a managed runtime with boxed primitives.

### Bottleneck analysis: Integer Loop (9x)

The integer loop creates 500M boxed integers (`AngaraObject {i32 tag, i64 payload}` wrapping an `ObjHeader`-carrying heap allocation). The 9x gap is NOT caused by GC collection — it's per-allocation overhead:

1. **gc_alloc cost:** Each allocation increments `gc_count`, compares against threshold, prepends to a linked list (store to global head), and returns. With 500M calls this dominates even though collections are rare (~7630 with threshold=65536).
2. **Tagged union boxing:** Every integer requires constructing an `AngaraObject` on the stack and a heap allocation for the boxed value. C operates on raw `i64` in registers.
3. **No allocation amortization:** Each `gc_alloc` is an individual `malloc`-equivalent call. A bump allocator would reduce this to a pointer increment.

Proof that GC collection is not the bottleneck: the gc_count fix (reset after sweep) reduced collections from ~500M to ~7630 with no measurable time difference (0.842s → 0.792s). The time is spent in the 500M individual gc_alloc calls, not in the ~7630 collections.

### Remaining optimization opportunities

- **Unboxed primitives** — avoiding boxing for integers in tight loops would eliminate the 9x gap entirely.

## Benchmark: ChaperoneGC vs MarkSweepGC vs C

After ChaperoneGC implementation (bump-arena alloc, compaction, arena recycling), `--release` / `-O2`, 5-run averages:

| Benchmark | Chaperone | MarkSweep | C -O2 | Chap/C | MS/C |
|---|---|---|---|---|---|
| Integer Loop (500M iter) | 0.020s | 0.020s | 0.018s | 1.11x | 1.11x |
| Prime Sieve (1M) | 0.028s | 0.032s | 0.028s | 1.00x | 1.14x |
| Recursive Fibonacci (n=40) | 0.284s | 0.272s | 0.280s | 1.01x | 0.97x |
| Matrix Multiply (200x200) | 0.018s | 0.016s | 0.018s | 1.00x | 0.88x |
| List Ops (1M items) | 0.016s | 0.018s | 0.016s | 1.00x | 1.12x |
| Bubble Sort (20K) | 0.416s | 0.424s | 0.522s | **0.79x** | 0.81x |
| String Building (100K) | 0.022s | 0.022s | 0.042s | **0.52x** | 0.52x |
| Data Class Churn (500K) | 0.086s | 0.084s | 0.016s | 5.37x | 5.25x |
| GC Stress (trees+strings+lists) | 0.032s | 0.036s | 0.046s | **0.69x** | 0.78x |

### Analysis

- **GC Stress beats C (0.69x)**. Bump-arena allocation is cheaper than malloc for short-lived objects. ChaperoneGC is 11% faster than MarkSweep on this benchmark.
- **String Building beats C (0.52x)**. The is_unique optimization enables in-place string concatenation.
- **Bubble Sort beats C (0.79x)**. Fewer allocations + cache-friendly arena layout.
- **Data Class Churn (5.37x)**. Still slow due to heap allocation per instance. Unboxed primitives would close this gap.
- **Most benchmarks at parity with C** (1.00x–1.11x). The GC overhead is negligible when allocation pressure is low.

## Stage 8: GC diagnostics

Add runtime introspection to measure memory behavior during execution.

### Goals
- Expose GC stats (live objects, total allocated, bytes used, collection count) via runtime functions
- Allow programs to query GC state for profiling and debugging
- Enable a future tool/IDE integration layer

### Tasks
- [ ] Add GC stats globals (collections_count, live_count, total_bytes)
- [ ] Generate `__ang_gc_stats` function returning snapshot
- [ ] Wire stats into collect/sweep/alloc
- [ ] Write memory benchmark program
- [ ] Verify: stats accurately reflect GC behavior
