# Freestanding Mode — Improvement Roadmap

> **Status: open.** This file is a backlog of concrete, agent-actionable
> improvements to Angara's `--freestanding` mode (bare-metal / no-libc targets).
> Each item lists the **root cause** (with `file:line` anchors), the **proposed
> fix**, the **scope of the change**, and **how to verify** it. Pick one item,
> read its anchors, implement, and check off the box.
>
> Companion docs:
> - [`23-bare-metal.md`](./23-bare-metal.md) — user-facing bare-metal guide.
> - [`FREESTANDING-INTRINSICS-PLAN.md`](./FREESTANDING-INTRINSICS-PLAN.md) — the
>   implemented Tiers 1–5 intrinsic set (this roadmap is the *next* layer).

---

## How freestanding mode works today (orientation for agents)

`--freestanding` is a strict compile mode that emits `_start` (not `main`) and
generates a stub runtime instead of the hosted one. The relevant code paths:

| Concern | Location |
|---------|----------|
| Flag parse | `angc/src/CLI.cpp:40` |
| Driver wiring | `angc/backend/driver/CompilerDriver.cpp:677, 969` |
| Stub runtime (the whole freestanding runtime) | `angc/backend/llvm/rt/Freestanding.cpp` |
| Runtime dispatch (`if (m_freestanding) { generateFreestandingStubs(); return; }`) | `angc/backend/llvm/RuntimeBuilder.cpp:19-27` |
| `_start` entry + halt loop | `angc/backend/llvm/TopLevel.cpp:1556-1604, 1780-1784` |
| Intrinsic name ladder (~60 intrinsics) | `angc/backend/llvm/expr/ExprCodegen.cpp:1665-2081` |
| LSP intrinsic table (duplicate of the ladder) | `angc/lsp/LSPServer.cpp:51-138` |
| Gates E910–E917 | `angc/analyzer/type_checker/{stmt,expr}/*.cpp` |
| E920 (asm outside `@unsafe`) | `angc/analyzer/type_checker/expr/AsmExpr.cpp:14` |
| E925 (privilege intrinsics outside `@privileged`) | `angc/analyzer/type_checker/expr/CallExpr.cpp:94-110` |
| `@unsafe` / `@privileged` parse | `angc/frontend/parser/Parser.cpp:178-193` |
| `@unsafe` / `@privileged` typecheck | `angc/analyzer/type_checker/stmt/{UnsafeBlockStmt,PrivilegedBlockStmt}.cpp` |
| Link skip (single-file) | `angc/src/CompileCommands.cpp:156-181` |
| Link skip (project build) | `angc/backend/build_system/BuildSystem.cpp:717-728` |
| Error-message text (E910–E925) | `angc/src/ExplainCommand.cpp:1037-1124` |
| Test runner (compile + `nm` contracts) | `tests/kernel/run_kernel_tests.sh` |
| IR-lowering test runner | `tests/kernel/run_ir_tests.sh` |

---

## P0 — Correctness (small, safe, do first)

### ☑ F1. `@unsafe` block does not save/restore its context flag *(done)*

**Root cause.** `angc/analyzer/type_checker/stmt/UnsafeBlockStmt.cpp:4-8`
unconditionally sets then clears `m_is_in_unsafe_context`:

```cpp
void TypeChecker::visit(std::shared_ptr<const UnsafeBlockStmt> stmt) {
    m_is_in_unsafe_context = true;                          // no save
    stmt->block->accept(*this, stmt->block);
    m_is_in_unsafe_context = false;                          // unconditionally clears
}
```

A nested `@unsafe { @unsafe { ... } /* still in outer block */ }` will, after
the inner block returns, leave `m_is_in_unsafe_context == false` for the
*remainder of the outer block* — silently disabling the outer block's unsafe
permissions. (The `@privileged` visitor at
`PrivilegedBlockStmt.cpp:7-14` does this correctly: it saves `was_unsafe` and
restores it on exit.)

**Fix.** Apply the same save/restore idiom the privileged block uses:

```cpp
void TypeChecker::visit(std::shared_ptr<const UnsafeBlockStmt> stmt) {
    bool was_unsafe = m_is_in_unsafe_context;
    m_is_in_unsafe_context = true;
    stmt->block->accept(*this, stmt->block);
    m_is_in_unsafe_context = was_unsafe;
}
```

**Scope.** One file, ~4 lines. No behavior change for non-nested blocks.

**Verify.** Add a negative test `tests/kernel/fs_unsafe_nesting.an` that nests
two `@unsafe` blocks and asserts an `asm(...)` in the outer block (after the
inner block closes) still type-checks. Run `bash tests/kernel/run_kernel_tests.sh`.

---

### ☑ F2. Freestanding runtime emits a GC/heap allocator it can never use *(done)*

**Root cause.** `angc/backend/llvm/rt/Freestanding.cpp:33-43` re-declares
`malloc`/`realloc`/`free` as external symbols and then calls
`generateMemoryManagement()` — the full hosted GC-frame allocator. The comment
at lines 28-32 admits this is a workaround for a former SIGSEGV (the allocator's
`getFunction("malloc")` returned null and `CreateCall` dereferenced it).

But the type checker **hard-rejects every heap feature** in freestanding mode
(E915 strings, E916 lists, E917 records — all in
`angc/analyzer/type_checker/`). So no source-level construct can allocate, yet
every freestanding object carries:
- dead GC frame-push/pop machinery in the emitted IR, and
- three external libc symbols (`malloc`/`realloc`/`free`) that bare-metal linker
  scripts must stub or the link fails.

**Fix.** Build a *minimal* freestanding runtime path that emits neither
`generateMemoryManagement()` nor the malloc/realloc/free decls. Keep:
- `__ang_equals` (real implementation, `Freestanding.cpp:45-58`),
- `__ang_api_throw_error` → `llvm.trap` + `unreachable` (lines 91-100),
- the no-op stubs for symbols the codegen may still reference (string/record/
  list/IO) so the verifier is happy (lines 59-85).

The risk is the frame-push/pop calls emitted by `TopLevel.cpp` — audit whether
`emitRtPushFrame`/`emitRtPopFrame` are gated on `!m_freestanding` (the main
function path is, at `TopLevel.cpp:1599-1604`; check per-function codegen in
`cgFunction`). If any call site still references the allocator, route it to a
no-op stub rather than reviving the malloc decls.

**Scope.** `Freestanding.cpp` + an audit of frame-push/pop call sites. Medium
care required (the original SIGSEGV shows the null-callee failure mode is
non-obvious).

**Verify.** `tests/kernel/fs_smoke.an` (the regression guard for the original
crash) must still pass. Add a check to `run_kernel_tests.sh` that the freestanding
object's undefined symbols do **not** include `malloc`/`realloc`/`free` (use
`nm -u`).

> **Implementation note (resolved).** Removing `generateMemoryManagement()` also
> required gating `LLVMBackend::createAllocatorInitFn()` (`LLVMBackend.cpp:318`)
> on `!m_freestanding`. That function is called unconditionally from both
> `generateIR` (line 146) and `generate` (line 170); its body calls
> `mod->getFunction("__ang_allocator_set")`, which is null without the memory
> layer, and `CreateCall(nullptr, ...)` segfaults. `m_allocator_init_fn` is only
> ever assigned (never read by the backend), so skipping it is safe. The
> `nm -u` heap-free assertion was added to `run_kernel_tests.sh` in the
> `--freestanding` PASS branch; `fs_intrinsics` now emits an object with **zero**
> undefined symbols.

---

## P1 — High-impact features

### ☑ F3. Add a no-heap `const` byte-array / byte-string literal *(done)*

**Why.** This is the single most felt gap. The type checker bans *all* strings
(E915) because the string runtime needs the heap. But a fixed-size byte literal
genuinely does **not** need the heap — it lowers to a `.rodata` blob.

Today, every bare-metal example works around this by hand-expanding strings into
dozens of `uart_putc(<ascii>)` calls:
- `examples/ankernel/kernel.an:25-44` — `"Hello from Angara!"` as 20 `uart_putc`
  calls.
- `examples/qemu_virt/kernel.an` and `examples/eye_trainer/eye.an` each
  re-implement a no-heap hex/decimal formatter from scratch.

**Proposed syntax** (sketch — confirm with the language owner):

```angara
// Fixed-size byte array, lowers to @rodata, no heap, no string runtime.
const BANNER as [u8; 14] = "Angara bare\n";

for i as i64 = 0; i < len(BANNER); i = i + 1 {
    uart_putc(BANNER[i] as i64);
}
```

Or, more conservatively, a `bytes`/`cstr` literal type that is the only
string-like construct permitted under `--freestanding`.

**Implementation surface:**
1. **Parse** a fixed-size array literal in `angc/frontend/parser/Parser.cpp`
   (new AST node or reuse `LiteralExpr` with a byte-array tag).
2. **Type-check**: permit this one construct under `--freestanding`; keep E915
   for the heap-backed `string` type. Add a `len` exception for byte arrays
   (currently `len` is gated wholesale at `CallExpr.cpp:79-93`).
3. **Codegen**: emit a global `@.rodata` constant and a `getelementptr`; this is
   the same lowering the hosted string path uses for its backing buffer, minus
   the header/length indirection.
4. **Subscript**: a `[u8; N]` should support indexing returning `u8` (the
   existing `SubscriptExpr` path).

**Scope.** Touches parser, type checker, codegen. This is a language feature,
not a patch — design it before implementing.

**Verify.** New test `tests/kernel/fs_bytelen.an`. Rewrite the `ankernel`
example to use a byte literal and confirm `make verify` (or the QEMU banner
assertion) still passes. Update `docs/23-bare-metal.md` "What works without
libc".

---

### ☐ F4. Atomics: add an LL/SC path for pre-8.1 cores

**Root cause.** Every `atomic_*` intrinsic lowers to LLVM atomic IR
(`ExprCodegen.cpp:1771-1818`), which the AArch64 backend emits as **LSE**
instructions (`cas`, `ldadd`, …). On a pre-ARMv8.1 core (Cortex-A53/-A57/-A72 —
real hardware like the Raspberry Pi 3 and many SoCs) these trap as undefined.
The docs (`23-bare-metal.md:96-99`) currently say "use `-cpu max`."

**Fix.** Add a lowering strategy switch, e.g. `--atomic-style=lse|llsc|auto`:
- `lse` (default today) — LLVM atomic IR → LSE.
- `llsc` — emit inline-asm load-linked/store-conditional sequences
  (`ldxr`/`stxr`, `ldaxr`/`stlxr` for acquire/release) per op.
- `auto` — driven by the target's `+lse`/`-lse` feature flag.

The LL/SC sequences are well-known; `atomic_cas` becomes a `ldaxr`/`stlxr` loop.
Keep monotonic as the default ordering (consistent with the LSE path).

**Scope.** `ExprCodegen.cpp` atomics block (1771-1818) — add a sibling lowering
path. Possibly a new CLI flag in `CLI.cpp`. Medium.

**Verify.** Extend `tests/kernel/run_ir_tests.sh` to assert the LL/SC
instructions appear under `--atomic-style=llsc`. Add a QEMU test booting with
`-cpu cortex-a53` (which lacks LSE) and asserting the atomics demo still runs.

---

### ☐ F5. `--target` validation: catch AArch64 intrinsics on the wrong arch

**Root cause.** The `--target` flag is passed straight through to clang
(`CompileCommands.cpp:104`). Nothing checks that the AArch64-only intrinsics
(`mrs`, `dc`, `tlbi`, `get_el`, …) are being emitted for an AArch64 target. A
user running `--freestanding --target x86_64-unknown-none-elf` who calls
`get_el()` gets a confusing *assembly-time* error from the inline-asm string
`"mrs $0, CurrentEL"`.

**Fix.** A frontend check: when an AArch64 intrinsic is called, verify the
target triple's arch is `aarch64` (or `arm64`). Emit a clear error (e.g. E926
"Intrinsic `get_el` requires an AArch64 target; got `<triple>`") at type-check
time rather than letting it fail in the assembler.

The intrinsic→arch mapping already exists implicitly in the codegen ladder;
surface it as metadata. The LSP table at `LSPServer.cpp:51-138` is a good place
to attach an `arch` field (see F8).

**Scope.** Type checker + a new error code. Small, once the intrinsic→arch
metadata exists.

**Verify.** `tests/kernel/fs_wrong_arch.an` — `--freestanding --target
x86_64-...` calling `get_el()` expects E926.

---

## P2 — Architecture & portability

### ☐ F6. RISC-V / x86 bare-metal intrinsic tiers

**Why.** Every intrinsic today is an AArch64 system register. There is no
RISC-V set (`csrrw`, `fence`, `sfence.vma`, `wfi`) and no x86 set (`cpuid`,
`rdtsc`, `invlpg`, `cli`/`sti`). If freestanding is a first-class mode, a
minimal RISC-V tier (CSRs + `fence` + `wfi`) would prove the architecture
abstraction and open up SiFive/ESP32-C3-class targets.

**Approach.** Define arch-agnostic intrinsic *roles* (e.g. "read cycle count",
"interrupt enable/disable", "fence") and map each to its arch-specific lowering.
Don't transliterate the AArch64 register names. A `--target=riscv64` build would
then expose `cntpct`→`rdcycle`, `disable_irq`→`csrsi mstatus, ...`, etc.

**Scope.** Large — a new intrinsic tier. Depends on F8 (table-driven registry)
to avoid doubling the name ladder. Design first.

**Verify.** Mirror the `tests/kernel/` suite for RISC-V (`fs_intrinsics_rv.an`,
`run_ir_tests_rv.sh`). A QEMU `virt` RISC-V boot demo.

---

### ☐ F7. GIC / interrupt-vector example and scaffolding

**Why.** `set_vbar` exists (`ExprCodegen.cpp:1964-2000`) but no example builds
the 16-byte-stride exception vector table AArch64 requires, and none talks to
the GIC. Every example stops at "poll a UART." The jump from "intrinsics exist"
to "you can write a real interrupt-driven driver" is unbridged.

**Fix.** Add `examples/qemu_virt_irq/` (or extend `qemu_virt/`) with:
- A vector table laid out at a known address (the 16 entries × 128-byte slots).
- A timer-IRQ demo: configure the generic timer, enable IRQs, handle the tick in
  an exception vector entry, print a counter to UART.
- A `Makefile` `verify-irq` target that boots QEMU and asserts the tick counter
  increments.

**Scope.** Example + docs only (no compiler change). Good first issue for
someone learning the freestanding model.

**Verify.** `make verify-irq` boots and the counter line advances.

---

## P3 — Infrastructure & quality-of-life

### ☑ F8. Table-driven intrinsic registry (replace the name ladder) *(done)*

**Root cause.** ~60 intrinsics are dispatched by a ~400-line
`if (fn == "...")` chain at `ExprCodegen.cpp:1665-2081`, **duplicated** in the
LSP completion table at `LSPServer.cpp:51-138`. Adding an intrinsic today means
editing codegen *and* the LSP table *and* the docs — three places that can fall
out of sync. (`FREESTANDING-INTRINSICS-PLAN.md` explicitly calls this "a ladder
of `if (fn == "...")` checks.")

**Fix.** A single source-of-truth registry, e.g.:

```cpp
struct IntrinsicDef {
    std::string name;
    std::string signature;        // for LSP hover + type-check
    std::string arch;             // "aarch64" | "any" — feeds F5
    std::string gate;             // "unsafe" | "privileged" | "none"
    IntrinsicLowerer lowerer;     // function pointer / lambda → LLVM IR
};
```

Populate one table; codegen iterates it, LSP reads it for completions, the
type checker reads the `gate`/`arch` fields. The docs table in
`23-bare-metal.md` could even be generated from it.

**Scope.** Refactor of the codegen dispatch + LSP table. Medium-large but
mechanical; improves every future intrinsic addition.

**Verify.** The existing `tests/kernel/run_ir_tests.sh` covers every intrinsic's
lowering — it is the regression net. After the refactor, all IR assertions must
still pass unchanged.

---

### ☐ F9. Wire QEMU boot tests into the central test runner

**Root cause.** `run_kernel_tests.sh` checks compile contracts (`nm`) and IR
lowering (`grep`), but never *executes* a freestanding image. `make verify` in
`qemu_virt/` does boot QEMU, but it's a per-example target, not part of the
central suite. A codegen→link→boot regression can slip past CI.

**Fix.** Add `tests/kernel/run_qemu_tests.sh` that builds `ankernel` (or
`qemu_virt`) and asserts the serial banner appears. Gate it on `qemu-system-aarch64`
being present (skip with a clear message if not). Chain it from
`run_kernel_tests.sh`.

**Scope.** New test script + Makefile wiring. Small.

**Verify.** It is the verification. Run it on a host with QEMU installed.

---

### ☐ F10. Document the `--freestanding` vs `--kernel` relationship

**Why.** The two modes are mutually exclusive (`CLI.cpp:46`) and their runtime
stubs are near-identical mirrors (`Freestanding.cpp` vs
`angc/backend/llvm/rt/Kernel.cpp`), but the relationship isn't summarized
anywhere a user would find it. Notably, lists are allowed in `--kernel` (see
`tests/kernel/ok_smoke.an`) but not in `--freestanding` (E916) — a user porting
between the two will hit this with no warning.

**Fix.** Add a "Freestanding vs kernel mode" comparison table to either
`23-bare-metal.md` or `KERNEL-MODE-IMPLEMENTATION.md`:
- Which heap features each permits.
- Entry-point symbol (`_start` vs none — kernel tests assert *no* `_start`/`main`,
  see `run_kernel_tests.sh:66-78`).
- When to use which (freestanding = no runtime at all; kernel = hosted runtime
  available, for in-kernel modules).

**Scope.** Docs only.

**Verify.** Manual review against the two runtime stub files.

---

## Priority summary

| ID | Item | Impact | Effort |
|----|------|--------|--------|
| ~~F1~~ | ~~`@unsafe` save/restore~~ ✅ | correctness | XS |
| ~~F2~~ | ~~Drop dead GC allocator from freestanding runtime~~ ✅ | correctness + bloat | M |
| ~~F3~~ | ~~No-heap `const` byte-array literal~~ ✅ | **high** (closes the biggest gap) | L (language feature) |
| F4 | LL/SC atomics for pre-8.1 cores | portability (real hardware) | M |
| F5 | `--target` arch validation | UX (clearer errors) | S |
| F6 | RISC-V / x86 intrinsic tiers | breadth | XL (depends on F8) |
| F7 | GIC / interrupt-vector example | onboarding | M (example only) |
| ~~F8~~ | ~~Table-driven intrinsic registry~~ ✅ | maintainability | M-L |
| F9 | QEMU boot tests in CI | regression safety | S |
| F10 | Document freestanding vs kernel | UX | XS |

**Suggested order for a new agent:** F1 → F2 (safe correctness fixes) → F8
(unblocks F4/F5/F6) → F3 (highest-impact feature) → F4 → F5.
