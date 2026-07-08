// ============================================================================
// driver_glue.c — kernel module glue for the /dev/angara character device.
//
// This is the unavoidable kernel-side glue: cdev registration, file_operations,
// copy_from_user/copy_to_user, and the module init/exit. It calls into the
// Angara-compiled object (driver.o → angara_module.o) for the per-call logic.
//
// ABI: Angara exported funcs take/return an AngaraObject = { i32 tag, i64 payload }
//      (16 bytes, verified from emitted IR). An i64 is boxed as { tag=2, payload=v }.
//      See angc/backend/llvm/rt/Memory.cpp for the tag enum (TAG_I64 == 2).
//
// Build contract:
//   1. driver.an is compiled by angc --kernel → angara_module.o (see Makefile).
//   2. module_init MUST call __ang_strlit_init_driver() once before any Angara
//      export, to initialize the module's string literals.
//   3. The kernel must be built with CC=clang so the Angara (LLVM) object and
//      kernel C objects share a toolchain.
// ============================================================================

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/kernel.h>

// --- Angara ABI ------------------------------------------------------------
//   Mirrors the %AngaraObject = type { i32, i64 } emitted by the backend.
//   i64 values are tagged TAG_I64 == 2. See RuntimeBuilder.cpp (TAG_* enum).
typedef struct {
    int  tag;       // 2 == i64
    long payload;   // the i64 value (or heap ptr for TAG_OBJ)
} AngaraObject;

static inline AngaraObject ang_box_i64(long v) {
    AngaraObject o = { .tag = 2, .payload = v };
    return o;
}
static inline long ang_unbox_i64(AngaraObject o) {
    return o.payload;   // caller must have checked tag if type matters
}

// --- Angara exports (defined in angara_module.o = driver.an) ---------------
extern void          __ang_strlit_init_driver(void);
extern void          __ang_allocator_init_driver(void *allocator);
extern void          __ang_mod_fini_driver(void);
extern AngaraObject  __ang_driver_angara_format(AngaraObject val);
extern AngaraObject  __ang_driver_angara_status(AngaraObject count);

// --- per-CPU arena (from kernel_runtime.c) --------------------------------
int  angara_install_arena_allocator(void (*module_init)(void *));
void angara_report_arena_stats(void);

// --- device state ----------------------------------------------------------
static int           major;
static struct cdev   angara_cdev;
static struct class *angara_class;
static struct device *angara_devnode;
static long          write_count;

// --- file_operations -------------------------------------------------------
static ssize_t angara_dev_write(struct file *f, const char __user *buf,
                                size_t n, loff_t *off) {
    long v = 0;
    size_t take = n < sizeof(long) ? n : sizeof(long);
    if (copy_from_user(&v, buf, take)) return -EFAULT;

    // Hand the value to Angara. angara_format doubles it and returns the
    // length of its formatted string label — exercises the full string runtime
    // (malloc/snprintf via the shim) on every write.
    AngaraObject res = __ang_driver_angara_format(ang_box_i64(v));
    long accepted = ang_unbox_i64(res);

    write_count++;
    return take;   // report bytes consumed
}

static ssize_t angara_dev_read(struct file *f, char __user *buf,
                               size_t n, loff_t *off) {
    // Report how many writes have been processed (pulls the count through the
    // Angara record runtime via angara_status).
    AngaraObject res = __ang_driver_angara_status(ang_box_i64(write_count));
    long status = ang_unbox_i64(res);

    if (n < sizeof(long)) return 0;
    if (copy_to_user(buf, &status, sizeof(long))) return -EFAULT;
    return sizeof(long);
}

static const struct file_operations angara_fops = {
    .owner = THIS_MODULE,
    .read  = angara_dev_read,
    .write = angara_dev_write,
};

// --- module init / exit ----------------------------------------------------
static int __init angara_mod_init(void) {
    int ret;
    dev_t devno;

    // 1. Initialize the Angara module's string literals. MUST run before any
    //    Angara export. (Today driver.an has no string literals so this is a
    //    no-op, but the contract must hold as the Angara side grows.)
    __ang_strlit_init_driver();

    // 2. Install the per-CPU arena allocator (optional). This swaps the
    //    module's default kmalloc-backed vtable for the arena via the external
    //    __ang_allocator_init_driver entry. Skip if you want plain kmalloc.
    ret = angara_install_arena_allocator(__ang_allocator_init_driver);
    if (ret) {
        pr_err("angara: arena allocator install failed: %d\n", ret);
        return ret;
    }

    // 3. Smoke-test the allocator path at init: build a record. If the libc
    //    shim's malloc/realloc/strdup are mis-wired, this oopses here at insmod
    //    rather than on the first user request.
    AngaraObject smoke = __ang_driver_angara_status(ang_box_i64(0));
    if (smoke.tag != 2) {
        pr_err("angara: init smoke test returned unexpected tag %d\n", smoke.tag);
        return -EINVAL;
    }

    // 3. Register a character device.
    major = register_chrdev(0, "angara", &angara_fops);
    if (major < 0) {
        pr_err("angara: register_chrdev failed: %d\n", major);
        return major;
    }
    devno = MKDEV(major, 0);

    cdev_init(&angara_cdev, &angara_fops);
    angara_cdev.owner = THIS_MODULE;
    ret = cdev_add(&angara_cdev, devno, 1);
    if (ret) {
        unregister_chrdev(major, "angara");
        pr_err("angara: cdev_add failed: %d\n", ret);
        return ret;
    }

    // 4. Create a /dev/angara node via sysfs/udev (no manual mknod needed).
    angara_class = class_create("angara");
    if (IS_ERR(angara_class)) {
        cdev_del(&angara_cdev);
        unregister_chrdev(major, "angara");
        return PTR_ERR(angara_class);
    }
    angara_devnode = device_create(angara_class, NULL, devno, NULL, "angara");
    if (IS_ERR(angara_devnode)) {
        class_destroy(angara_class);
        cdev_del(&angara_cdev);
        unregister_chrdev(major, "angara");
        return PTR_ERR(angara_devnode);
    }

    write_count = 0;
    pr_info("angara: /dev/angara ready (major=%d)\n", major);
    return 0;
}

static void __exit angara_mod_exit(void) {
    dev_t devno = MKDEV(major, 0);
    device_destroy(angara_class, devno);
    class_destroy(angara_class);
    cdev_del(&angara_cdev);
    unregister_chrdev(major, "angara");
    // Reclaim the Angara module's persistent heap state (string-literal globals,
    // etc.) before reporting arena stats — so freed blocks return to the arena
    // free list and the stats reflect the finalize/frees.
    __ang_mod_fini_driver();
    angara_report_arena_stats();
    pr_info("angara: unloaded\n");
}

module_init(angara_mod_init);
module_exit(angara_mod_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Angara-sourced character device (kernel-mode example)");
MODULE_AUTHOR("Angara");
