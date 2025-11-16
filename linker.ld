OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

SECTIONS
{
    . = 1M;

    .boot : {
        *(.multiboot)
        *(.text)
        *(.text.*)
    }

    . = ALIGN(4K);
    .rodata : {
        *(.rodata)
        *(.rodata.*)
        /* Trampoline code embedded in rodata */
        trampoline_start = .;
        *(.trampoline)
        trampoline_end = .;
    }

    . = ALIGN(4K);
    .data : {
        *(.data)
        *(.data.*)
    }

    . = ALIGN(4K);
    .bss : {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }

    /DISCARD/ : {
        *(.eh_frame)
        *(.note*)
        *(.comment)
    }
}
