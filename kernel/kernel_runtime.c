// ============================================================================
// kernel_runtime.c — Angara libc shim for Linux kernel modules.
//
// This is the ONLY C code a kernel-mode Angara module needs to link against
// (besides its own driver_glue.c). It bridges the libc surface that Angara's
// emitted runtime IR references to the kernel's own equivalents.
//
// IMPORTANT — why this file is so small:
//   Angara does NOT have a separate runtime library. Every __ang_* runtime
//   function is compiled directly into each Angara object as InternalLinkage
//   LLVM IR by the compiler's RuntimeBuilder. So the shim provides ZERO __ang_*
//   symbols — it only satisfies the libc symbols the embedded runtime calls.
//   Every allocation path (the Allocator vtable AND the "raw" malloc/realloc
//   sites in the collection constructors) funnels through the single module-
//   level malloc/realloc/free declaration, so mapping those three to
//   kmalloc/krealloc/kfree routes ALL Angara heap traffic into the kernel
//   allocator uniformly.
//
// Verified UND surface of a --kernel object (nm -u):
//   malloc, realloc, free, memcpy, memset, strlen, strdup, snprintf
// plus the angara_kernel_print* entry points that generateKernelIO emits.
//
// Kernel build contract:
//   - The C-side module_init MUST call __ang_strlit_init_<module>() exactly
//     once before invoking any exported Angara func from that module.
//   - The kernel must be built with CC=clang so Angara's clang/LLVM object and
//     the kernel's C objects come from one toolchain and link cleanly.
// ============================================================================

#include <linux/slab.h>
#include <linux/printk.h>
#include <linux/kernel.h>
#include <linux/string.h>

// ---------------------------------------------------------------------------
// libc allocator bridge  →  kmalloc / krealloc / kfree
//
// GFP_KERNEL is process-context-safe. If Angara code may run in atomic/softirq
// context, switch these to GFP_ATOMIC (or provide a per-call variant).
// Note: krealloc handles old==NULL and new==0 per libc semantics, but kfree is
// single-arg; the embedded runtime always calls free with one argument.
// ---------------------------------------------------------------------------

void *malloc(unsigned long size) {
    if (size == 0) return NULL;            // libc malloc(0) may return NULL or a unique ptr
    return kmalloc(size, GFP_KERNEL);
}

void *realloc(void *ptr, unsigned long new_size) {
    return krealloc(ptr, new_size, GFP_KERNEL);
}

void free(void *ptr) {
    kfree(ptr);                            // kfree(NULL) is explicitly safe
}

// strdup: the record-key copy path (generateRecordOps) uses libc strdup.
char *strdup(const char *s) {
    return kstrdup(s, GFP_KERNEL);         // kstrdup returns NULL only on OOM
}

// snprintf: used by __ang_to_string for i64/f64 formatting. The kernel
// provides snprintf natively with the same signature.

// memcpy / memset / strlen: the kernel provides these with libc-compatible
// signatures — no shim needed. (Freestanding mode replaces them with IR loops;
// kernel mode keeps the libc decls and lets them resolve to the kernel impls.)

// ---------------------------------------------------------------------------
// IO bridge  →  printk
//
// generateKernelIO emits these three entry points (ExternalLinkage) and routes
// __ang_io_print/println/write through them. Stream id 2 (stderr) selects the
// _err variant. printk is unbuffered, so flush is a no-op (handled in IR).
// ---------------------------------------------------------------------------

void angara_kernel_print(const char *s) {
    printk(KERN_INFO "%s", s);
}

void angara_kernel_println(const char *s) {
    printk(KERN_INFO "%s\n", s);
}

void angara_kernel_print_err(const char *s) {
    printk(KERN_ERR "%s", s);
}

// ---------------------------------------------------------------------------
// Defensive stubs for symbols the *skipped* generators would otherwise leave
// dangling if a stray reference survived. In normal --kernel builds these are
// unreferenced and dropped by the linker; providing them keeps an accidental
// reference (e.g. an unguarded try sneaking through a dependency) from turning
// into a cryptic module-load link error.
// ---------------------------------------------------------------------------

// exit(): only the skipped exception-abort path references it.
void exit(int status) {
    (void)status;
    panic("Angara runtime: exit() invoked in kernel context\n");
}

// strtod(): intentionally NOT provided. The kernel is built with
// -mgeneral-regs-only (no FP registers in kernel context), so a `double`-typed
// stub would not even compile. __ang_to_f64 references strtod, but it is only
// pulled in if Angara code uses f64 — which is discouraged in-kernel. If you
// do need f64, wrap the call site with kernel_fpu_begin()/kernel_fpu_end()
// and provide a strtod that returns 0 or uses a fixed-point parse.
