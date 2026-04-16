/*
 * boot.S — ARM64 bare-metal boot stub for QEMU virt machine
 *
 * Sets up stack, clears BSS, and jumps to the Angara-generated _start.
 * QEMU virt loads the kernel at 0x40080000.
 */

.section ".text.boot"

.global _entry

_entry:
    // Only run on CPU 0 — park all other CPUs
    mrs     x0, mpidr_el1
    and     x0, x0, #0xFF
    cbz     x0, cpu0_boot
park:
    wfe
    b       park

cpu0_boot:
    // Set up stack pointer (top of 128MB RAM region)
    ldr     x0, =_stack_top
    mov     sp, x0

    // Clear BSS section
    ldr     x0, =_bss_start
    ldr     x1, =_bss_end
clear_bss:
    cmp     x0, x1
    b.ge    bss_done
    str     xzr, [x0], #8
    b       clear_bss
bss_done:

    // Jump to Angara-generated _start
    bl      _start

    // Should never return here, but just in case
    b       park