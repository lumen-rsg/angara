// ============================================================================
// timer_glue.c — kernel module owning a timer that calls into Angara each tick.
//
// Pull-model callback: the C side owns struct timer_list and calls the Angara
// exports on_tick / accumulate on each expiry. This avoids the FFI trampoline
// (which is synchronous-only: it frees the callback context after the foreign
// call returns, so a deferred timer fire would dereference freed memory).
//
// ABI: AngaraObject = { i32 tag, i64 payload }; i64 boxed as { tag=2, payload=v }.
// See driver_glue.c for the same boxing helpers.
// ============================================================================

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/printk.h>

typedef struct {
    int  tag;       // 2 == i64
    long payload;
} AngaraObject;

static inline AngaraObject ang_box_i64(long v) {
    AngaraObject o = { .tag = 2, .payload = v };
    return o;
}
static inline long ang_unbox_i64(AngaraObject o) {
    return o.payload;
}

// Angara exports (defined in timer.o = timer.an).
extern void          __ang_strlit_init_timer(void);
extern void          __ang_allocator_init_timer(void *allocator);
extern void          __ang_mod_fini_timer(void);
extern AngaraObject  __ang_timer_on_tick(AngaraObject count);
extern AngaraObject  __ang_timer_accumulate(AngaraObject history);

// Per-CPU arena (from kernel_runtime.c).
int  angara_install_arena_allocator(void (*module_init)(void *));
void angara_report_arena_stats(void);

static struct timer_list ang_timer;
static atomic_long_t     tick_count     = ATOMIC_LONG_INIT(0);
static atomic_long_t     accumulate_acc = ATOMIC_LONG_INIT(0);
static bool              stopping = false;

// The timer callback: fires every second, calls into Angara.
static void ang_timer_cb(struct timer_list *t) {
    long count = atomic_long_inc_return(&tick_count);

    // Per-tick Angara call: build a status string in Angara, get its length.
    AngaraObject r = __ang_timer_on_tick(ang_box_i64(count - 1));
    long status_len = ang_unbox_i64(r);

    // A second Angara export driving a running accumulator.
    long acc = atomic_long_read(&accumulate_acc);
    AngaraObject a = __ang_timer_accumulate(ang_box_i64(acc));
    atomic_long_set(&accumulate_acc, ang_unbox_i64(a));

    // Log every 5th tick so dmesg isn't flooded.
    if (count % 5 == 0) {
        pr_info("angara-timer: tick %ld (status_len=%ld, acc=%ld)\n",
                count, status_len, atomic_long_read(&accumulate_acc));
    }

    if (!stopping) {
        mod_timer(&ang_timer, jiffies + msecs_to_jiffies(1000));
    }
}

static int __init angara_timer_init(void) {
    // Initialize the Angara module's string literals first.
    __ang_strlit_init_timer();
    // Then install the per-CPU arena allocator (optional).
    int ret = angara_install_arena_allocator(__ang_allocator_init_timer);
    if (ret) return ret;

    stopping = false;
    timer_setup(&ang_timer, ang_timer_cb, 0);
    mod_timer(&ang_timer, jiffies + msecs_to_jiffies(1000));
    pr_info("angara-timer: armed, firing every 1s (invoke on_tick/accumulate)\n");
    return 0;
}

static void __exit angara_timer_exit(void) {
    stopping = true;
    timer_delete_sync(&ang_timer);
    pr_info("angara-timer: disarmed after %ld ticks (final acc=%ld)\n",
            atomic_long_read(&tick_count),
            atomic_long_read(&accumulate_acc));
    __ang_mod_fini_timer();
    angara_report_arena_stats();
}

module_init(angara_timer_init);
module_exit(angara_timer_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Angara kernel timer example (pull-model callback)");
MODULE_AUTHOR("Angara");
