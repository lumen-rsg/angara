/* Linker script for ARM64 bare-metal Angara kernel (QEMU virt) */

ENTRY(_entry)

SECTIONS
{
    /* QEMU virt machine loads the kernel at this address */
    . = 0x40080000;

    /* Boot code must come first */
    .text : {
        *(.text.boot)
        *(.text .text.*)
    }

    /* Read-only data */
    .rodata : {
        *(.rodata .rodata.*)
    }

    /* Initialized data */
    .data : {
        *(.data .data.*)
    }

    /* BSS — zero-initialized data */
    . = ALIGN(16);
    _bss_start = .;
    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    }
    _bss_end = .;

    /* Stack (64KB) */
    . = ALIGN(16);
    . += 0x10000;
    _stack_top = .;

    /* Discard unneeded sections */
    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame*)
    }
}