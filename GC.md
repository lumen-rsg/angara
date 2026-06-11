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

### Stage 1: Infrastructure
- [ ] Create GC interface class
- [ ] Modify ObjHeader layout
- [ ] Add GC globals
- [ ] Generate `__ang_gc_alloc`
- [ ] Update header initialization sites
- [ ] Verify: compiles and runs identically

### Stage 2: Remove ARC from codegen
- [ ] Remove decref from cgAssign
- [ ] Remove decref from main cleanup
- [ ] Remove incref from makeStr
- [ ] Verify: compiles, runs, leaks memory

### Stage 3: Remove ARC from runtime
- [ ] Remove incref/decref from Collections, Strings, ControlFlow, IO, Conversions
- [ ] Replace malloc with gc_alloc in constructors
- [ ] Add env_count to closure layout
- [ ] Add clear_unique at container stores
- [ ] Verify: compiles, runs, no manual memory management

### Stage 4: Implement the collector
- [ ] Implement __ang_gc_scan (type-dispatched traversal)
- [ ] Implement __ang_gc_mark (tri-color marking)
- [ ] Implement __ang_gc_mark_roots (thread list → root frames → mark)
- [ ] Implement __ang_gc_sweep (walk allocation list)
- [ ] Implement __ang_gc_collect (mark_roots + sweep)
- [ ] Wire threshold check into gc_alloc
- [ ] Verify: GC works single-threaded

### Stage 5: Root frames + thread registration
- [ ] Implement push_frame / pop_frame
- [ ] Implement thread_register / thread_unregister
- [ ] Implement safepoint (stop-the-world)
- [ ] Add root frame setup to codegen
- [ ] Add thread registration to main + thread trampoline
- [ ] Verify: multi-threaded GC with precise root scanning

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
