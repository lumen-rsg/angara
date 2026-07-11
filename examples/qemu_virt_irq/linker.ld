/* Linker script for ARM64 bare-metal Angara kernel with interrupts (QEMU virt).
 * QEMU's -kernel loader places the image at 0x40080000 and jumps to it. */

ENTRY(_entry)

SECTIONS
{
    . = 0x40080000;
    .text : {
        *(.text.boot)
        *(.text .text.*)
    }
    .rodata : {
        *(.rodata .rodata.*)
    }
    .data : {
        *(.data .data.*)
    }
    . = ALIGN(16);
    _bss_start = .;
    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    }
    _bss_end = .;
    . = ALIGN(16);
    . += 0x10000;          /* 64 KiB stack */
    _stack_top = .;
    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame*)
    }
}
