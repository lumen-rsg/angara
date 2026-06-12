# ChaperoneGC: Protein-Folding Inspired Garbage Collection

## Title

**Protein-Folding Inspired Heuristics for Concurrent Heap Organization: Reducing Compaction Overhead in Garbage Collection**

## Biological Foundation

### The Hydrophobic Effect

In molecular biology, protein folding is driven by the **hydrophobic effect**: non-polar (water-fearing) residues collapse inward to form a dense core, while polar (water-loving) residues remain exposed on the surface. This spontaneous self-organization produces compact, functional 3D structures without external intervention. The process minimizes **free energy** — a composite of enthalpic interactions (hydrogen bonds, van der Waals forces) and entropic contributions (chain conformation, solvent ordering).

### Chaperone Molecules

In living cells, **molecular chaperones** (e.g., Hsp60/GroEL, Hsp70) assist protein folding without becoming part of the final structure. They do not dictate the folded state — instead, they prevent misfolding, resolve toxic aggregates, and guide partially-folded intermediates back toward their native energy minimum. Chaperones operate continuously and incrementally, never requiring a global "reset" of the cell's protein population.

### Mapping to Garbage Collection

| Biology | Garbage Collection |
|---------|-------------------|
| Protein residue | Heap object |
| Hydrophobic residue (buried) | Hot object (frequently referenced, high "reference heat") |
| Hydrophilic residue (exposed) | Cold object (rarely accessed, dead or dying) |
| Free energy | Fragmentation + pointer distance + cache miss estimate |
| Native fold (energy minimum) | Dense, cache-friendly object layout |
| Misfolded aggregate | Fragmented heap with scattered live objects |
| Chaperone molecule | Background GC thread that incrementally relocates objects |
| Thermal fluctuation | Simulated annealing temperature for stochastic optimization |

## Architecture

The ChaperoneGC augments a conventional mark-and-sweep collector with a protein-folding-inspired background process. The core principle is not to replace compaction, but to make it unnecessary most of the time by continuously reducing heap entropy.

### Three Mechanisms

1. **Bump-Arena Allocation** (replaces per-object malloc)
   - Thread-local arenas with bump pointers
   - Allocation cost reduced to a single pointer increment
   - Objects within an arena are contiguous — already partially "folded"

2. **Energy Landscape Computation** (the "folding" analogy)
   - Each arena has a computed energy: E = w_f * fragmentation + w_d * pointer_distance + w_c * cache_estimate
   - Hot objects (high reference heat, many incoming pointers) should cluster — like hydrophobic residues forming a core
   - Cold objects (few references, candidates for sweep) should drift to arena edges — like hydrophilic residues on the protein surface
   - The chaperone thread continuously evaluates this landscape

3. **Simulated Annealing Relocation** (the "chaperone" analogy)
   - The background thread proposes small object moves (bounded cluster size per step)
   - Accepts moves that reduce energy (improve locality)
   - Accepts energy-increasing moves with probability exp(-delta_E / T) — thermal fluctuation
   - Temperature cools over time, then periodically reheats (premature convergence avoidance)
   - Moves are executed atomically with forwarding pointers — no global pause required

### ObjHeader Layout (16 bytes, same as MarkSweepGC)

```
{ i32 type, i32 meta, i8* forward }
```

- `type`: Object subtype (OBJ_STRING, OBJ_LIST, etc.)
- `meta`: Packed bitfield
  - Bits 0-7: Color (WHITE=0, GRAY=1, BLACK=2, FORWARDED=3)
  - Bit 8: is_unique (in-place mutation optimization)
  - Bit 16: pinned (prevents relocation/collection)
  - Bits 24-31: arena_id (which arena owns this object)
- `forward`: Forwarding pointer for relocation. Null during normal operation.

### ArenaHeader Layout

```
{ i8* base, i8* bump, i8* limit, i64 object_count, i32 arena_id, i8* next_arena }
```

Each arena is a contiguous block of memory. Objects are bump-allocated within it. The sweep phase walks arena contents contiguously instead of following a linked list.

### Read Barrier

Every pointer dereference checks the FORWARDED color. If set, the `forward` field points to the object's new location. This allows the chaperone to relocate objects while mutators continue running — the forwarding pointer acts as a "molecular address change" that any code following the reference automatically resolves.

## Progress

### Stage 0: Prerequisite Refactoring
- [x] Add virtual methods to GarbageCollector interface (getInitialMetaConstant, getReadBarrierFunc, setAngaraObjType, setStructTypes)
- [x] Replace MarkSweepGC::packMeta calls with virtual dispatch (17 sites across 3 files)
- [x] Remove MarkSweepGC.h includes from runtime files (6 files)
- [x] Add identity read barrier to MarkSweepGC
- [x] Verify: compiles and runs identically

### Stage 1: Bump-Arena Allocation
- [x] Create ChaperoneGC.h / ChaperoneGC.cpp
- [x] ArenaHeader type + bump allocation
- [x] Linked-list sweep (forward field as next pointer)
- [x] Thread-local arena management
- [x] Wire into RuntimeBuilder via USE_CHAPERONE_GC ifdef
- [x] TLS null-safety in alloc and alloc_slow
- [x] Fix: sweep must not free() bump-allocated objects (arena owns memory)
- [x] Fix: only trigger GC collection when over threshold
- [x] Verify: all LLVM tests pass (hello_llvm, stress_llvm, test_classes, etc.)

### Stage 2: Read Barrier
- [x] Implement __ang_gc_read_barrier (checks FORWARDED color, follows forward ptr)
- [x] Integrate into mark chain
- [x] Verify: no regression (read barrier never triggers yet — no relocation)

### Stage 3: Chaperone Thread
- [x] Background thread lifecycle (__ang_gc_chaperone_main + pthread_create)
- [x] Energy computation per arena (fragmentation metric: 0.4 * (1 - live_ratio))
- [x] Simulated annealing optimizer (cool T*=0.9999, reheat every 10k steps, min T=0.01)
- [x] Incremental relocation with forwarding pointers (__ang_gc_relocate: memcpy + FORWARDED color)
- [x] Reference update protocol (__ang_gc_update_refs: walk roots, fix TAG_OBJ payloads)
- [x] Lazy chaperone spawn on first arena allocation
- [x] GC running check: chaperone skips step when GC is active
- [x] Verify: all LLVM tests pass with chaperone thread running

### Stage 4: Optimizations
- [x] Free-list within arenas (arena.free_list field, reused slots from sweep)
- [x] Arena_id in object meta (bits 24-31) for free-list routing during sweep
- [x] Unified alloc patching: all return paths link into allocation list
- [x] Generational hints via arena_id (older arenas = more stable objects)
- [ ] Inline read barrier (codegen change)
- [ ] Multi-chaperone threads

## Notes

- The "forward" field (index 2) replaces the "next" field from MarkSweepGC. Same GEP index, same initial value (null). Binary-compatible with existing runtime code.
- Arena size: 1MB default (configurable). 256 arenas max = 256MB pre-allocated.
- Simulated annealing parameters: T_initial=1.0, cooling=0.9999, T_min=0.01, reheat cycle=10000 steps.
- Energy weights: w_fragmentation=0.4, w_pointer_distance=0.35, w_cache=0.25.
