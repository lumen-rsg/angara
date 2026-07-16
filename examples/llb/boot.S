/*
 * boot.S — ARM64 bare-metal boot stub for the Angara LLB (Linux Loader/Bootloader).
 *
 * Sets up the stack, clears BSS, parks secondary cores, and branches to the
 * Angara-generated _start. Also embeds the Linux kernel Image and device tree
 * blob (DTB) as binary blobs via .incbin.
 *
 * QEMU virt loads the binary at 0x40080000 and enters at EL1.
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

/* ── Embedded Linux kernel Image ───────────────────────────────────────
 * Place Image in the same directory as this file. It is the raw arm64
 * kernel Image produced by:
 *   make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- Image -j$(nproc)
 * (output: arch/arm64/boot/Image)
 */
.section ".kernel","a"
.align 3
.global _kernel_start
_kernel_start:
.incbin "Image"
.global _kernel_end
_kernel_end:

/* ── Embedded device tree blob (DTB) ───────────────────────────────────
 * Generate with:
 *   qemu-system-aarch64 -machine virt -cpu cortex-a53 -m 128 \
 *       -M dumpdtb=qemu_virt.dtb
 * Place qemu_virt.dtb in the same directory as this file.
 */
.section ".dtb_blob","a"
.align 3
.global _dtb_start
_dtb_start:
.incbin "qemu_virt.dtb"
.global _dtb_end
_dtb_end:
