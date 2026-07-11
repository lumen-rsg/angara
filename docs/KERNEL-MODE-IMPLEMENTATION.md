# Angara Kernel Mode — Implementation Plan & Linux Driver Guide

> **Status: FULLY VERIFIED END-TO-END (July 2026).** The `/dev/angara`
> character driver built from `.an` source loads and runs inside a running
> Linux kernel on asahi arm64:
> - `insmod angara_drv.ko` → `angara: /dev/angara ready (major=503)` (the
>   Angara `__ang_strlit_init_driver` + record-building smoke test both ran
>   in-kernel, allocating through `kmalloc`/`kstrdup` via the libc shim).
> - `echo 7 > /dev/angara` → `head -c8 /dev/angara` → `0000000000000001`
>   round-trip through two exported Angara funcs (`angara_format`, which
>   builds a string; `angara_status`, which builds a record).
>
> Phases 1–5 are implemented and verified:
> - `--kernel` flag, curated kernel runtime (`generateKernelRuntime` + kernel
>   IO via `printk`), entry-point suppression (no `_start`/`main`), libc link
>   skip with `-o` honoring + cross-device (`EXDEV`) fallback.
> - Frontend hard-error gates: `throw` E900, `try`/`catch` E901, `spawn` E902,
>   `Mutex` E903, `attach` of a native module E904. Gated tests in
>   `tests/kernel/` (`make test-kernel`, 4/4).
> - The worked `/dev/angara` driver, libc shim (`kernel/kernel_runtime.c`),
>   and Kbuild bridge in `kernel/`.
>
> **Two findings from the actual insmod:**
> 1. **A gcc-built kernel works with `CC=gcc`** — the original doc's "must use
>    CC=clang" was overly conservative. Angara objects are plain relocatable
>    ELF with standard relocations, so they link cleanly against either
>    toolchain. `CC=clang` only works if the *kernel itself* was clang-built
>    (a gcc-built kernel's CFLAGS use gcc-specific flags clang rejects). For
>    the asahi host (gcc-built), use `CC=gcc`.
> 2. **The Linux module loader rejects COMMON symbols** (`__ang_exception_chain`
>    was `CommonLinkage` → insmod failed with "please compile with
>    -fno-common"). Fixed at the compiler source: both `__ang_exception_chain`
>    (`RuntimeBuilder.cpp:194`) and `__ang_rt_thread_state` (`Memory.cpp:28`)
>    are now `InternalLinkage` with explicit zero init, so all `--kernel`
>    modules are COMMON-free by default.
>
> **Swappable allocator now reachable (verified in-kernel):** the
> `__ang_allocator_set` vtable was InternalLinkage and completely unreachable;
> the new module-qualified `__ang_allocator_init_<mod>` (External) lets external
> C install a custom allocator. A per-CPU slab arena in the shim routes real
> heap traffic through it — `insmod angara_drv.ko` + one write + `rmmod`:
> ```
> angara: /dev/angara ready (major=503)
> angara-arena: 5 allocs, 4 frees, 5 arena hits, 0 kmalloc fallbacks
> ```
> **5/5 allocations served by the arena, zero fell through to kmalloc** — the
> custom vtable exclusively handled the module's heap. The 4 frees are the
> module's 2 string-literal globals reclaimed at `rmmod` via
> `__ang_mod_fini_<mod>` (each: chars interior + string header = 2 frees × 2
> literals). (Hosted proof too: `make test-kernel` installs a counting
> allocator and observes list/record/string growth routing through it, plus the
> fini freeing the literals.)
>
> **rmmod reclaims module-persistent state (`__ang_mod_fini_<mod>`):** a
> module-qualified External teardown finalizes+frees the string-literal globals
> (and the mechanism extends to module-var globals) via the runtime's
> `__ang_rt_finalize`+`__ang_rt_free`, routed through the swapped allocator.
> A literal "walk all live objects" is impossible — `ObjHeader.next` is dead
> (vestigial from the removed GC; `CHAPERONE.md` decision #10) — so fini covers
> the *enumerable* persistent globals, not every heap object. Per-call leaks
> *inside* exported funcs (the Chaperone's W501) are a separate, programmer-
> responsibility problem, not fixable by an rmmod hook.
>
> **Several load-bearing assumptions in the original design were CORRECTED by
> reading the source** — these are marked `✅ CORRECTION` below. The original
> prose is retained for context; the corrections are authoritative.

## TL;DR

> **New to kernel mode?** See the
> [Freestanding vs kernel mode](./23-bare-metal.md#freestanding-vs-kernel-mode)
> comparison in the Bare-Metal Programming guide for a quick overview of what
> each mode permits and when to use which.

Angara already has most of the structural support for kernel-mode targets: a
`--freestanding` flag that emits a `_start` entry and skips libc-init/native-module
glue, a `--nostdlib` flag, a **swappable runtime allocator** (the old GC was already
replaced with an `Allocator` vtable routable via `__ang_allocator_set`), and a
`foreign func` FFI that lowers to plain C ABI calls. What is **missing** is:

1. A kernel-safe runtime shim (allocator + panic) that replaces libc.
2. A `kernel` build profile + entry-point convention distinct from `_start`.
3. A Linux kernel-module Makefile/Kbuild bridge that links Angara object files
   into a `.ko`.
4. Careful discipline around what Angara features are *kernel-safe* (no
   exceptions-with-unwinding, no native modules, no spawn).

This document walks through each, with concrete code paths and a worked example:
a character device driver (`/dev/angara`) compiled from `.an` source.

---

## 1. Current state — what the compiler already gives you

Verify these before starting; they're the load-bearing assumptions.

### 1.1 Freestanding entry & runtime skip

`angc/backend/llvm/TopLevel.cpp:1515-1723` (`codegenMainFunction`):
- `m_freestanding` → entry symbol is **`_start`** (void return) instead of `main` (i32).
- Skips `emitRtThreadSetup`, `emitRtPushFrame`, the native-module `Angara_<mod>_Init`
  registration loop, and the runtime teardown/stats.
- In freestanding mode the entry ends in an infinite `halt` loop
  (`TopLevel.cpp:1708-1712`) instead of `ret exit_code`.

CLI flag: **`--freestanding`**. Also relevant: **`--nostdlib`**, **`--target <triple>`**,
**`--sysroot <path>`**, **`--emit-llvm`**, **`-l <file>`** (link extra objects).

### 1.2 Swappable allocator (the big enabler)

`angc/backend/llvm/rt/Memory.cpp:1-120`. The GC was removed; all allocations route
through a single `%Allocator = { ptr alloc(i64), ptr realloc(ptr,i64,i64),
ptr free(ptr,i64) }` vtable. The default wraps libc `malloc/realloc/free`.

Runtime hooks:
- `__ang_allocator_get() -> ptr` — read current allocator.
- `__ang_allocator_set(ptr) -> void` — swap the global allocator pointer.
- `__ang_rt_alloc(i64 size, i32 type) -> i8*` — the internal alloc path all
  Angara heap objects (`__ang_record_new`, `__ang_string_new`, closures, etc.) use.

**Implication:** a kernel module can, at init, call `__ang_allocator_set` with a
vtable whose functions wrap `kmalloc`/`krealloc`/`kfree`. Every Angara allocation
then lands in the kernel allocator with zero changes to the codegen.

> ⚠️ **✅ CORRECTION (verified by source, July 2026): the "~30 bypass sites" do
> NOT block kernel mode.** Every `malloc_fn`/`realloc_fn`/`free_fn` call across
> `Collections.cpp`, `Strings.cpp`, `Conversions.cpp`, `IO.cpp`,
> `ControlFlow.cpp`, `ModuleAPI.cpp`, `ExprCodegen.cpp`, `LLVMBackend.cpp`, and
> `TopLevel.cpp` resolves to the *single* module-level `malloc`/`realloc`/`free`
> declaration in `declareCLibFunctions` (`RuntimeBuilder.cpp:227-229`). So if the
> kernel shim maps those three to `kmalloc`/`krealloc`/`kfree`, **every**
> allocation path — vtable-backed *and* the "raw" call sites *and* the interior
> `free()` calls in `__ang_rt_finalize` (`Memory.cpp:310-438`) — lands in the
> kernel allocator uniformly. Verified: a `--kernel` object's UND surface is
> exactly `malloc/realloc/free/memcpy/snprintf/strdup/strlen` (`nm -u`).
>
> The original concern below (rewrite the ~30 codegen sites) is now **deferred
> optional hardening**, not a prerequisite. It would only matter if you wanted a
> non-`kmalloc` allocator routed via `__ang_allocator_set` (e.g. a per-CPU arena).
>
> Original notes (retained for context, now non-blocking):
> Spot-check `Collections.cpp` showed the swappable allocator was used for object
> **headers** (`__ang_rt_alloc`) but several runtime constructors allocate
> **backing buffers** with a raw libc `malloc_fn`/`realloc_fn` call: the
> `RawArray` element buffer (`Collections.cpp:467`), list growth (`:151`,`:512`),
> record entry table (`:782`). There are ~30 such sites tree-wide
> (`Strings.cpp`, `Conversions.cpp`, `IO.cpp`, `ControlFlow.cpp`, `ModuleAPI.cpp`,
> `ExprCodegen.cpp`, `LLVMBackend.cpp`, `TopLevel.cpp`). All funnel through the
> single `malloc`/`realloc`/`free` declaration, so the shim handles them. The
> swappable-allocator (`__ang_allocator_set`) routing of these sites remains a
> future optimization, not a v1 requirement.

### 1.3 FFI (`foreign func`)

`foreign func` declarations lower to direct C-ABI calls. Argument/result
marshalling is in `LLVMBackend.cpp` (`marshalAngaraToC` / `marshalCToAngara`) and
the foreign-wrapper codegen in `TopLevel.cpp:~1300-1416`. This is exactly the
machinery that lets kernel code call into kernel C APIs (printk, cdev, etc.) and
that lets C call back into Angara (the trampoline path, now leak-free — see M25).

`foreign data` declares C structs with inline-array/field layout
(`foreign data utsname { sysname as i8[256]; ... }`).

### 1.4 What does NOT work in a kernel (must be avoided)

- **Native modules** (`attach io;`, `attach http;`, ...) — they are `.so`s loaded
  via `dlopen`/RTLD_LOCAL. A kernel has no dynamic loader. Freestanding mode
  already skips the init loop, so any `attach` of a userspace module is a
  compile/link error in kernel mode (good — enforce it).
- **`spawn`** — creates a pthread. No pthreads in the kernel.
- **Exceptions across C frames** — the trampoline wraps callback invocations in
  `setjmp` (`LLVMBackend.cpp:~996-1050`). `setjmp` is not available/meaningful
  in kernel context. Kernel-mode Angara must be **no-throw** across FFI (the
  `@on_throw` value is the fallback). Inside pure Angara code, exception
  unwinding is fine *if* you bring a minimal unwind/panic stub.
- **Floating point** — kernel context may not save FP registers. Keep Angara
  kernel code integer-only unless you wrap with `kernel_fpu_begin/end`.

---

## 2. What needs to be built — work breakdown

### Phase 1: Kernel runtime shim (the core missing piece)

> **✅ CORRECTION (verified by source, July 2026): the shim provides NO `__ang_*`
> symbols.** Every `__ang_*` runtime function is emitted *directly into the
> compiled module* as `InternalLinkage` IR by `RuntimeBuilder::createRuntimeFunc`
> (`RuntimeBuilder.cpp:46-52`). There is no `libangara_rt.a`; the `rt/*.cpp` files
> are `RuntimeBuilder` methods that *generate IR*. `__ang_fatal` (referenced in
> the original sketch below) **does not exist anywhere** in the codebase.
>
> The real shim (`kernel/kernel_runtime.c`, implemented) is a pure **libc bridge**:
> it maps `malloc`/`realloc`/`free`/`strdup` → `kmalloc`/`krealloc`/`kfree`/
> `kstrdup`, provides `angara_kernel_print*` (the entry points `generateKernelIO`
> calls, → `printk`), and defensive `exit`/`strtod` stubs. The original sketch's
> `__ang_allocator_set` extern + vtable-swap is unnecessary: satisfying
> `malloc`/`realloc`/`free` directly routes all allocations to the kernel
> allocator. The swappable-allocator feature remains orthogonal/future.

The freestanding runtime still emits libc-backed allocator defaults. For a kernel,
you need a small C shim that the Angara program links against, providing:

**File: `kernel_runtime.c` (new, ~150 lines)**
```c
#include <linux/slab.h>
#include <linux/printk.h>

// 1) Allocator vtable backed by kmalloc/krealloc/kfree.
//    Shape must match %Allocator in Memory.cpp: { alloc, realloc, free }.
typedef void* (*alloc_fn)(unsigned long);
typedef void* (*realloc_fn)(void*, unsigned long, unsigned long);
typedef void  (*free_fn)(void*, unsigned long);
struct AngaraAllocator { alloc_fn alloc; realloc_fn realloc; free_fn free; };

static void* k_alloc(unsigned long size)            { return kmalloc(size, GFP_KERNEL); }
static void* k_realloc(void* p, unsigned long oldsz, unsigned long newsz) {
    return krealloc(p, newsz, GFP_KERNEL);
}
static void k_free(void* p, unsigned long size)     { kfree(p); }
static struct AngaraAllocator kernel_allocator = { k_alloc, k_realloc, k_free };

// 2) Called from the module's _start equivalent (see Phase 2) before any Angara code.
extern void __ang_allocator_set(void*);   // from Memory.cpp runtime
void angara_kernel_init(void) {
    __ang_allocator_set(&kernel_allocator);
}

// 3) Panic hook — Angara's runtime calls __ang_fatal on unrecoverable errors.
//    Wire it to a kernel panic so it never returns.
void __ang_fatal(const char* msg) {
    panic("Angara runtime: %s\n", msg);
}
```

> The exact signatures of `__ang_allocator_set`, `__ang_rt_alloc`, and the fatal
> hook must be confirmed against `Memory.cpp` and the codegen sites that emit
> calls to them. Read `angc/backend/llvm/rt/Memory.cpp` end-to-end and grep the
> backend for every `callRtByName("__ang_...")` to enumerate the full surface the
> shim must satisfy.

**Acceptance:** an Angara program that allocates a record, returns, and frees
nothing should run under the kernel allocator with no oops, verified by
`kmemleak`.

### Phase 2: Build profile + entry convention

Freestanding emits `_start` and halts. A Linux kernel module needs
`init_module`/`cleanup_module` (or `module_init`/`module_exit`) and must NOT
`return` out of `init`. Two options:

**Option A (recommended, minimal compiler change): compile Angara to an object
file with no entry point, and write the module glue in C.**

The cleanest path: extend the freestanding mode to emit a **relocatable object**
(`-c` equivalent) instead of a linked binary with `_start`. Then:

- `kernel_driver.an` exports the driver logic as Angara `export func`s.
- `driver_glue.c` is the actual kernel module (uses `module_init`, registers
  `cdev`, etc.) and calls into the Angara `export func`s by their mangled names.
- The Angara object + glue C + `kernel_runtime.c` are linked by the kernel's
  Kbuild into `angara_drv.ko`.

This keeps the compiler change tiny: just **emit object files** (LLVM `Reloc::PIC_`
is already set — `LLVMBackend.cpp:56`) and skip the `_start` synthesis when an
"object-only" flag is passed.

**Required compiler change (small):**
- New flag `--kernel` (or reuse `--freestanding` + `-c`) that:
  1. Suppresses `codegenMainFunction` entirely (no `_start`).
  2. Marks `export func`s with external linkage (already the case).
  3. Refuses any `attach` of a non-kernel module (compile error).
- Wire `--kernel` through `CompilerDriver` → `LLVMBackend` (mirror how
  `m_freestanding` flows at `CompilerDriver.cpp:997`).

**Option B (larger): emit `module_init`/`module_exit` directly from Angara.**
Add `@kernel_init` / `@kernel_exit` attributes the codegen lowers to the kernel
convention. More work, and you lose the clean C-glue boundary. Defer unless you
want pure-Angara modules.

### Phase 3: Kbuild bridge

A Makefile that hands Angara objects to the kernel build system. Linux Kbuild
expects `.o` (ELF relocatable) inputs compiled with kernel CFLAGS. The mismatch
to solve: Angara's clang/LLVM output vs the kernel's compiler.

**Recommended:** build the kernel with clang (Linux supports `CC=clang`). Then
Angara object files and kernel C files come from the same toolchain and link
cleanly.

**File: `Kbuild`**
```make
obj-m := angara_drv.o
# Angara-compiled object + C glue + runtime shim
angara_drv-y := driver_glue.o kernel_runtime.o angara_module.o
```

**File: `Makefile` (wrapper to invoke angc + kbuild)**
```make
ANGC := angc
KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	# 1. Compile Angara source to a relocatable object (Phase 2 flag).
	$(ANGC) --kernel --target $(shell uname -m)-unknown-linux-gnu \
	    driver.an -o angara_module.o
	# 2. Build the kernel module via standard kbuild.
	$(MAKE) -C $(KDIR) M=$(PWD) modules
```

The `.o` Angara emits must be ELF relocatable with `-fPIC`/kernel-compatible
relocations. `LLVMBackend.cpp:54-56` already sets `Reloc::PIC_`; verify the
emitted object links with `ld -r` against kernel symbols before trusting it.

### Phase 4: Kernel-safe feature gate

Add a compile-time gate so kernel modules can't accidentally pull in
userspace-only machinery. At minimum, in `--kernel` mode:
- **Hard error** on `attach <userspace_module>` (any module whose init would
  dlopen). The freestanding path already skips the init loop; make it a
  *diagnostic*, not a silent skip.
- **Hard error** on `spawn` (maps to pthreads).
- **Warn** on floating-point types/operations unless wrapped.

This is mostly a pass through the type-checker / a new `Context` flag analogous
to the existing `ctx.in_unsafe` used by the Chaperone.

---

## 3. Worked example — a `/dev/angara` character driver

This is the target end state. The Angara side is the device logic; the C side is
unavoidable kernel glue (cdev registration, file_operations).

### 3.1 `driver.an` — Angara device logic

```angara
// Kernel FFI declarations — straight C ABI into the kernel.
foreign data file;
foreign func printk(fmt as *i8, ...) -> i32;

// A simple ring buffer of i64, allocated via the Angara allocator (→ kmalloc).
// Angara's ownership/drop discipline (the Chaperone, now sound after the H1/H2
// closure fixes) guarantees no leak across open/release.
export func angara_write(val as i64) -> i64 {
    // ...store into ring buffer...
    return 0;
}

export func angara_read() -> i64 {
    // ...pop from ring buffer...
    return 42;
}

// Called by the C glue at module init to swap in the kmalloc allocator.
export func angara_init() -> i32 {
    angara_kernel_init();   // foreign func → sets the kernel allocator vtable
    return 0;
}
```

### 3.2 `driver_glue.c` — kernel module + cdev

```c
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>

extern int  angara_init(void);
extern long angara_write(long val);
extern long angara_read(void);

static int major;
static struct cdev angara_cdev;

static ssize_t angara_dev_write(struct file *f, const char __user *buf,
                                size_t n, loff_t *off) {
    long v = 0;
    if (copy_from_user(&v, buf, min(n, sizeof(long)))) return -EFAULT;
    return angara_write(v);
}
static ssize_t angara_dev_read(struct file *f, char __user *buf,
                               size_t n, loff_t *off) {
    long v = angara_read();
    if (copy_to_user(buf, &v, sizeof(long))) return -EFAULT;
    return sizeof(long);
}
static const struct file_operations angara_fops = {
    .owner = THIS_MODULE, .read = angara_dev_read, .write = angara_dev_write,
};

static int __init angara_mod_init(void) {
    int ret = angara_init();          // swap allocator, init Angara state
    if (ret) return ret;
    major = register_chrdev(0, "angara", &angara_fops);
    cdev_init(&angara_cdev, &angara_fops);
    return cdev_add(&angara_cdev, MKDEV(major, 0), 1);
}
static void __exit angara_mod_exit(void) {
    cdev_del(&angara_cdev);
    unregister_chrdev(major, "angara");
}
module_init(angara_mod_init);
module_exit(angara_mod_exit);
MODULE_LICENSE("GPL");
```

### 3.3 Build, load, test
```bash
make                        # angc → angara_module.o; kbuild → angara_drv.ko
sudo insmod angara_drv.ko   # creates /dev/angara via udev rule or mknod
echo 7 >  /dev/angara
head -c8 /dev/angara
sudo rmmod angara_drv
```

---

## 4. Soundness & safety posture (why now)

This plan is viable *because* of the audit close-out that preceded it:

- **Ownership/escape analysis is sound.** The Chaperone's closure-borrow bug
  (H1/H2 over-generalization) is fixed — returning `x.get()` no longer falsely
  transfers ownership. In a kernel, a false E503/E501 or a missed leak is far
  costlier than in userspace, so a trustworthy ownership checker is the
  prerequisite. Run `make test-chaperone` (96/96) as the gate before trusting
  any kernel build.
- **FFI callback lifetime is leak-free** (M25). Kernel drivers with callbacks
  (interrupt handlers, timers) would otherwise leak per-registration under load.
  Verified under valgrind; the free-after-call model is correct for the
  synchronous foreign-func path kernel code will use.
- **Default-secure FFI.** Build steps require `--allow-build-steps` (C5 fix);
  flag injection is blocked (H14); SSL/config-path traversal is guarded (L8).

**Remaining caveats for kernel use:**
- **M26 (enum `is`)** is still deferred — `is EnumType` returns true for any heap
  object. Avoid `is` on enum types in kernel code; use `match` (which reads
  `__tag` and is correct) instead.
- **No async** in kernel mode — `spawn` and `await` depend on pthreads/exceptions.
  A kernel-cooperative async would be a separate, larger effort.
- **Incremental builds can lie.** During this very session a `make` incremental
  no-op'd a relink after a compile error and left a stale binary with debug code
  no longer in source. **Always do `make clean && make -j<N>` when iterating on
  codegen.** This matters acutely for kernel work where a stale object can produce
  silent miscompiles.

---

## 5. Implementation order & checkpoints

| Step | Deliverable | Verify |
|------|-------------|--------|
| 1 | **Audit all allocations in `rt/Memory.cpp` + `rt/Collections.cpp`**: fix the ~5 raw `malloc_fn`/`realloc_fn` call sites to route through `__ang_rt_alloc` so the swappable allocator is honored uniformly. | Grep shows zero direct libc malloc/realloc in runtime constructors; `test-cpp` + `test-chaperone` still green |
| 2 | Read `Memory.cpp` end-to-end; enumerate every remaining `__ang_*` runtime symbol the codegen calls | List of symbols the shim must provide |
| 3 | Write `kernel_runtime.c` (kmalloc allocator + panic) | Unit-link against an Angara object in a `qemu` freestanding harness |
| 4 | Add `--kernel` flag: object-only emit, no `_start`, refuse `attach`/`spawn` | `angc --kernel t.an -o t.o` produces relocatable ELF (`readelf -r`) |
| 5 | Build the worked `/dev/angara` driver in QEMU (`virtio-console` to test read/write) | `echo`/`cat` round-trips; `kmemleak` clean; `rmmod` clean |
| 6 | Add Chaperone gate: `make test-chaperone` as a hard CI check before any kernel build | 96/96 |

Steps 1–2 are the gating investigation — they decide how large the shim actually
is. Do not estimate them; read the code.

---

## 6. Open questions to resolve before committing

1. **Exception model in-kernel:** does Angara's try/catch require libgcc
   unwinding, or is it setjmp-based? `ControlFlow.cpp` uses `setjmp`/`__ang_try_*`
   — likely portable to kernel with a `setjmp` from kernel headers, but verify a
   throw inside Angara code (not crossing FFI) actually unwinds in-kernel.
   **✅ ANSWERED (verified by source):** try/catch is **setjmp/longjmp-based,
   not DWARF-unwind** — `cgTry` calls `setjmp` *directly* via
   `mod->getFunction("setjmp")` (`StmtCodegen.cpp:492`), `__ang_throw` calls
   `longjmp` (`ControlFlow.cpp:276`). BUT: `declareCLibFunctions()` (which
   declares setjmp) is skipped in freestanding mode, so `getFunction("setjmp")`
   returns null → `CreateCall(null)` **crashes the compiler at IR-build time** on
   any `try`/`catch` in freestanding mode (and the FFI trampoline at
   `LLVMBackend.cpp:1010` has the same defect). For `--kernel` we chose to
   **hard-error try/catch/throw** (E900/E901) in the type-checker, sidestepping
   this entirely. Supporting in-kernel exceptions would require declaring
   setjmp/longjmp in the kernel path + providing them in the shim + emitting a
   real `__ang_throw` — deferred.
2. **Object file ABI:** does the Angara-emitted `.o` use relocations the kernel
   linker accepts? Test with `ld -r angara_module.o -o combined.o` early.
   **✅ ANSWERED:** the Angara object is a standard ELF relocatable
   (`Reloc::PIC_`, `CodeModel::Small`); the C-glue `extern` decls resolve to
   the object's mangled exports (`__ang_<mod>_<fn>`), and `AngaraObject` =
   `{i32 tag, i64 payload}` (16 B) matches the C `{int tag; long payload}`.
   `ld -r` of glue + Angara object succeeds (ABI verified). arm64 modules are
   position-independent, so **`Reloc::PIC_` is correct** for the native-host
   target — the original "want `Reloc::Static`" note is wrong for arm64.
3. **String/list/record allocation is NOT fully routed through the swappable
   allocator** (see the ⚠️ in §1.2). The header allocations are; the backing
   buffers (raw `malloc_fn`/`realloc_fn` in `Collections.cpp`) are not. This must
   be fixed in codegen (route them through `__ang_rt_alloc`) **before** Phase 1's
   shim can be trusted. This is now the first real work item, not an open question.
   **✅ CORRECTION:** this was the original plan's first work item and it is
   **not required**. All "raw" sites resolve to the single `malloc`/`realloc`/
   `free` declaration, which the shim maps to `kmalloc`/`krealloc`/`kfree`. See
   the §1.2 correction above. Routed through the swappable allocator only
   matters for a future non-kmalloc allocator.
4. **Concurrency:** kernel code is preemptible/SMP. The Angara allocator vtable
   swap is global. Decide whether per-CPU allocators or a spinlock-guarded
   allocator is needed (kmalloc is already GFP-safe, so likely fine).
   **✅ RESOLVED for v1:** the kernel shim provides plain `kmalloc`/`kfree`
   (`GFP_KERNEL`, already SMP-safe); `__ang_allocator` is read-only in this
   model. Per-CPU arenas remain a future optimization.

Resolve these with small spike programs in QEMU before building the full driver.
