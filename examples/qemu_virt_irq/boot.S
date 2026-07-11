/*
 * boot.S — ARM64 bare-metal boot stub + exception vector table for QEMU virt.
 *
 * Sets up the stack, clears BSS, installs the exception vector table, and
 * jumps to the Angara-generated _start. The IRQ handler stub saves the
 * register file, calls the Angara irq_handler(), restores, and returns via
 * eret.
 *
 * This is the only non-Angara code in the example. The vector table itself
 * is data (16 entries × 0x80 bytes at fixed offsets from VBAR_EL1) that
 * cannot be laid out from inline asm; the register save/restore needs
 * precise stack-frame control. Everything else — GIC programming, timer
 * setup, handler logic — is pure Angara.
 */

.section ".text.boot"

.global _entry

_entry:
    mrs     x0, mpidr_el1
    and     x0, x0, #0xFF
    cbz     x0, cpu0_boot
park:
    wfe
    b       park

cpu0_boot:
    /* Stack */
    ldr     x0, =_stack_top
    mov     sp, x0

    /* Clear BSS */
    ldr     x0, =_bss_start
    ldr     x1, =_bss_end
clear_bss:
    cmp     x0, x1
    b.ge    bss_done
    str     xzr, [x0], #8
    b       clear_bss

bss_done:
    /* Install the exception vector table before enabling IRQs. */
    ldr     x0, =_vectors
    msr     vbar_el1, x0
    isb

    bl      _start
    b       park

/* ── IRQ handler stub ────────────────────────────────────────────────────
 * Called from the vector table (offset 0x280). Saves the full register file
 * (x0–x30 + sp_el0 + elr_el1 + spsr_el1), calls the Angara irq_handler(),
 * restores everything, and returns via eret. Uses sp for the save frame so
 * the handler (compiled by Angara) sees a normal AAPCS stack.
 */
.section ".text"
.balign 16
.global el1_irq_handler
el1_irq_handler:
    sub     sp, sp, #256
    stp     x0,  x1,  [sp, #0]
    stp     x2,  x3,  [sp, #16]
    stp     x4,  x5,  [sp, #32]
    stp     x6,  x7,  [sp, #48]
    stp     x8,  x9,  [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x19, [sp, #144]
    stp     x20, x21, [sp, #160]
    stp     x22, x23, [sp, #176]
    stp     x24, x25, [sp, #192]
    stp     x26, x27, [sp, #208]
    stp     x28, x29, [sp, #224]
    str     x30,      [sp, #240]

    bl      __ang_kernel_irq_handler

    ldr     x30,      [sp, #240]
    ldp     x28, x29, [sp, #224]
    ldp     x26, x27, [sp, #208]
    ldp     x24, x25, [sp, #192]
    ldp     x22, x23, [sp, #176]
    ldp     x20, x21, [sp, #160]
    ldp     x18, x19, [sp, #144]
    ldp     x16, x17, [sp, #128]
    ldp     x14, x15, [sp, #112]
    ldp     x12, x13, [sp, #96]
    ldp     x10, x11, [sp, #80]
    ldp     x8,  x9,  [sp, #64]
    ldp     x6,  x7,  [sp, #48]
    ldp     x4,  x5,  [sp, #32]
    ldp     x2,  x3,  [sp, #16]
    ldp     x0,  x1,  [sp, #0]
    add     sp, sp, #256

    eret

/* ── Sync exception handler (catches unexpected faults) ────────────────── */
.section ".text"
.balign 16
el1_sync_handler:
    /* Print "SYNC\n" to UART0 to aid debugging, then halt. */
    ldr     x1, =0x09000000      /* UART_DR */
    mov     w0, #'S'
    str     w0, [x1]
    mov     w0, #'Y'
    str     w0, [x1]
    mov     w0, #'N'
    str     w0, [x1]
    mov     w0, #'C'
    str     w0, [x1]
    mov     w0, #'\n'
    str     w0, [x1]
1:  wfe
    b       1b

/* ── Exception vector table ──────────────────────────────────────────────
 * AArch64 VBAR_EL1 points here. 16 entries, each 0x80 (128) bytes apart,
 * organized as 4 exception types × 4 SP modes. At EL1 with SPx, the CPU
 * vectors to offset 0x280 for IRQ. Each slot holds a branch; the .balign
 * 0x80 between them ensures the fixed offsets.
 */
.section ".text"
.global _vectors
.balign 2048
_vectors:
    /* CurrentEL with SP0: 0x000–0x180 (we don't use SP0, but must fill) */
    .balign 0x80, 0
    b   el1_sync_handler
    .balign 0x80, 0
    b   el1_irq_handler          /* 0x080: IRQ, SP0 */
    .balign 0x80, 0
    b   el1_sync_handler         /* 0x100: FIQ, SP0 */
    .balign 0x80, 0
    b   el1_sync_handler         /* 0x180: SError, SP0 */

    /* CurrentEL with SPx: 0x200–0x380 (THIS is where EL1 IRQ fires) */
    .balign 0x80, 0
    b   el1_sync_handler         /* 0x200: Sync, SPx */
    .balign 0x80, 0
    b   el1_irq_handler          /* 0x280: IRQ, SPx ← */
    .balign 0x80, 0
    b   el1_sync_handler         /* 0x300: FIQ, SPx */
    .balign 0x80, 0
    b   el1_sync_handler         /* 0x380: SError, SPx */

    /* Lower EL, AArch64: 0x400–0x580 (not expected at EL1 bare metal) */
    .balign 0x80, 0
    b   el1_sync_handler         /* 0x400 */
    .balign 0x80, 0
    b   el1_sync_handler         /* 0x480 */
    .balign 0x80, 0
    b   el1_sync_handler         /* 0x500 */
    .balign 0x80, 0
    b   el1_sync_handler         /* 0x580 */

    /* Lower EL, AArch32: 0x600–0x780 (not expected) */
    .balign 0x80, 0
    b   el1_sync_handler         /* 0x600 */
    .balign 0x80, 0
    b   el1_sync_handler         /* 0x680 */
    .balign 0x80, 0
    b   el1_sync_handler         /* 0x700 */
    .balign 0x80, 0
    b   el1_sync_handler         /* 0x780 */
