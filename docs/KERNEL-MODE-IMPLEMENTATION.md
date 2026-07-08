# Angara Kernel Mode — Implementation Plan & Linux Driver Guide

> **Status:** Design document. Based on the angc codebase state as of the soundness-audit close-out (July 2026). This is an engineering plan, not a finished feature.

## TL;DR

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

> ⚠️ **Verify before relying on this (this is the single biggest risk in the plan).**
> Spot-check `angc/backend/llvm/rt/Collections.cpp` shows the swappable allocator is
> used for object **headers** (`__ang_rt_alloc`), but several runtime constructors
> allocate **backing buffers** with a *raw libc* `malloc_fn`/`realloc_fn` call:
> `RawArray` element buffer (`Collections.cpp:467`), list growth (`:151`,`:512`),
> record entry table (`:782`). These bypass `__ang_allocator_set`.
> **This means** that in kernel mode, lists/records/strings (when their backing
> store grows) would call libc `malloc` directly — which doesn't exist in a kernel.
> **Two fixes are possible** (pick before Phase 1):
>   (a) **CodeGen fix (recommended):** change those ~5 raw `malloc_fn`/`realloc_fn`
>       call sites to route through `__ang_rt_alloc`/a runtime realloc hook, so the
>       swap is honored uniformly. Small, localized change.
>   (b) **Shim fix:** make the kernel's libc-compat layer provide `malloc`/`realloc`
>       that wrap `kmalloc`. Fragile (header sizing, `free` size ambiguity).
> Audit every `CreateCall(malloc_fn...)` / `realloc_fn` / `strcmp_fn` in
> `Collections.cpp` and `Memory.cpp` — that enumeration *is* the shim's required
> surface.

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
2. **Object file ABI:** does the Angara-emitted `.o` use relocations the kernel
   linker accepts? Test with `ld -r angara_module.o -o combined.o` early.
3. **String/list/record allocation is NOT fully routed through the swappable
   allocator** (see the ⚠️ in §1.2). The header allocations are; the backing
   buffers (raw `malloc_fn`/`realloc_fn` in `Collections.cpp`) are not. This must
   be fixed in codegen (route them through `__ang_rt_alloc`) **before** Phase 1's
   shim can be trusted. This is now the first real work item, not an open question.
4. **Concurrency:** kernel code is preemptible/SMP. The Angara allocator vtable
   swap is global. Decide whether per-CPU allocators or a spinlock-guarded
   allocator is needed (kmalloc is already GFP-safe, so likely fine).

Resolve these with small spike programs in QEMU before building the full driver.
