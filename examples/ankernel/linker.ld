/* Linker script for ARM64 bare-metal Angara kernel (QEMU virt) */

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
    . += 0x10000;
    _stack_top = .;
    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame*)
    }
}