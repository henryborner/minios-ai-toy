OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)

SECTIONS
{
    . = 0x300000;

    .text : {
        KEEP(entry.o(.text))
        *(.text)
        *(.text.*)
    }

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    .data : {
        *(.data)
        *(.data.*)
    }

    __bss_start = .;
    .bss (NOLOAD) : {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }
    __bss_end = .;
}

