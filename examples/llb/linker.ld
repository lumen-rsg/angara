/*
 * Linker script for the Angara LLB (Linux Loader/Bootloader) on QEMU virt.
 *
 * QEMU's -kernel loader places the raw binary at 0x40080000. The LLB code and
 * read-only data go first, followed by the embedded Linux kernel Image and the
 * embedded device tree blob. BSS and stack are at the end.
 *
 * QEMU virt memory map (128 MiB):
 *   0x40000000 – RAM start
 *   0x40080000 – LLB load address (this binary)
 *   0x42000000 – kernel destination (2 MiB aligned, copied at runtime)
 *   0x48000000 – RAM end
 */

ENTRY(_entry)

SECTIONS
{
    . = 0x40080000;

    /* ── Boot code (must be first — QEMU jumps here) ── */
    .text : {
        *(.text.boot)
        *(.text .text.*)
    }

    /* ── Read-only data ── */
    .rodata : {
        *(.rodata .rodata.*)
    }

    /* ── Mutable data ── */
    .data : {
        *(.data .data.*)
    }

    /* ── Embedded Linux kernel Image (from boot.S .incbin "Image") ── */
    .kernel : {
        _kernel_start = .;
        *(.kernel)
        _kernel_end = .;
    }

    /* ── Embedded device tree blob (from boot.S .incbin "qemu_virt.dtb") ── */
    .dtb_blob : {
        _dtb_start = .;
        *(.dtb_blob)
        _dtb_end = .;
    }

    /* ── BSS ── */
    . = ALIGN(16);
    _bss_start = .;
    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    }
    _bss_end = .;

    /* ── Stack (64 KiB, grows downward) ── */
    . = ALIGN(16);
    . += 0x10000;
    _stack_top = .;

    /* ── Discard debug / comment sections to keep the binary small ── */
    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame*)
    }
}
