/*
 * boot.S — ARM64 bare-metal boot stub for QEMU virt machine
 *
 * Sets up stack, clears BSS, and jumps to the Angara-generated _start.
 * QEMU virt loads the kernel at 0x40080000.
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
    ldr     x0, =_stack_top
    mov     sp, x0
    ldr     x0, =_bss_start
    ldr     x1, =_bss_end
clear_bss:
    cmp     x0, x1
    b.ge    bss_done
    str     xzr, [x0], #8
    b       clear_bss
bss_done:
    bl      _start
    b       park