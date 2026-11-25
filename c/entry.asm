; 32-bit kernel entry stub.  The boot sector has already installed flat
; segments and landed at 0x10000 with interrupts off.
        BITS 32
        global _start
        extern kmain
        extern __bss_start
        extern __bss_end

        section .text
KERNEL_BASE  equ 0x00300000
KERNEL_BYTES equ 131072

_start:
        cli
        mov     esp, 0x90000
        call    .here
.here:
        pop     eax
        cmp     eax, KERNEL_BASE
        jae     .high

        ; Boot sector loaded the kernel at 0x10000.  Move it to 3 MB.
        mov     esi, 0x00010000
        mov     edi, KERNEL_BASE
        mov     ecx, KERNEL_BYTES / 4
        cld
        rep     movsd
        jmp     .high_entry

.high:
.high_entry:
        mov     ebp, esp

        ; zero .bss (objcopy binary does not contain it)
        mov     edi, __bss_start
        mov     ecx, __bss_end
        sub     ecx, edi
        xor     eax, eax
        cld
        rep     stosb

        call    kmain

.hang:
        cli
        hlt
        jmp     .hang
