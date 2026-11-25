OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)

SECTIONS
{
    . = 0x00400000;

    .ump_header : {
        KEEP(*(.ump_header))
    }

    .text : ALIGN(16) {
        *(.text)
        *(.text.*)
    }

    .rodata : ALIGN(16) {
        *(.rodata)
        *(.rodata.*)
    }

    .data : ALIGN(4096) {
        __ro_size = . - 0x00400000;
        *(.data)
        *(.data.*)
    }

    . = ALIGN(16);
    __image_end = .;

    __bss_start = .;
    .bss (NOLOAD) : ALIGN(16) {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }
    . = ALIGN(16);
    __bss_end = .;

    __entry_offset = _start - 0x00400000;
    __image_size   = __image_end - 0x00400000;
    __bss_size     = __bss_end - __bss_start;

    /DISCARD/ : {
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.comment)
        *(.note*)
    }
}
